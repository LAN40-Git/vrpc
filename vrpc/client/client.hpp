#pragma once
#include <kosio/net.hpp>
#include <kosio/sync.hpp>
#include <tbb/concurrent_hash_map.h>
#include "vrpc/core/detail/config.hpp"
#include "vrpc/core/detail/protocol.hpp"
#include "vrpc/core/detail/request.hpp"

namespace vrpc {
class Client {
    using CallbackMap = tbb::concurrent_hash_map<uint64_t, Callback>;

    enum class Status {
        kConnecting,
        kConnected,
        kDisconnected,
        kShutdown
    };

public:
    explicit Client(std::string_view server_ip, uint16_t server_port)
        : server_ip_(server_ip)
        , server_port_(server_port)
        , stream_(kosio::net::detail::Socket{-1}) {
        auto [sender, receiver] =
                            vrpc::detail::RequestChannel::make(vrpc::detail::REQUEST_CHANNEL_CAPACITY);
        sender_ = std::move(sender);
        receiver_ = std::move(receiver);
    }

    // Delete copy
    Client(const Client&) = delete;
    auto operator=(const Client&) -> Client& = delete;

    // Delete move
    Client(Client&&) = delete;
    auto operator=(Client&&) -> Client& = delete;

public:
    template <typename T>
    requires requires(const T& t) {
        { t.SerializeAsString() } -> std::same_as<std::string>;
    }
    [[REMEMBER_CO_AWAIT]]
    auto call(
        Type service_type,
        Type invoke_type,
        const T& request,
        const Callback& callback) -> kosio::async::Task<StatusCode> {
        static std::atomic<uint64_t> current_request_id{0};

        auto request_id = current_request_id.fetch_add(1);
        auto req_payload = request.SerializeAsString();
        if (req_payload.size() > detail::MAX_MESSAGE_SIZE) {
            co_return StatusCode::kResourceExhausted;
        }
        auto reqeust = detail::Request{request_id, service_type, invoke_type, std::move(req_payload), callback};

        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        // 通道关闭，RPC 服务不可用
        if (status_ == Status::kShutdown) {
            co_return StatusCode::kUnavailable;
        }
        // 连接未建立
        if (status_ == Status::kDisconnected) {
            // 若未启动连接建立协程，则启动
            if (status_ != Status::kConnecting) {
                status_ = Status::kConnecting;
                kosio::spawn(connect());
            }
        }
        co_await sender_.send(reqeust);
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> kosio::async::Task<void> {
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        status_ = Status::kShutdown;
        co_await latch_.wait();
    }

private:
    auto connect() -> kosio::async::Task<void> {
        auto has_addr = kosio::net::SocketAddr::parse(server_ip_, server_port_);
        if (!has_addr) {
            LOG_ERROR("{}", has_addr.error());
            co_return;
        }
        auto addr = has_addr.value();

        auto has_stream = co_await kosio::net::TcpStream::connect(addr);
        if (!has_stream) {
            LOG_ERROR("{}", has_addr.error());
            co_return;
        }

        if (auto ret = has_stream.value().set_nodelay(true); !ret) {
            LOG_ERROR("{}", ret.error());
            co_return;
        }

        LOG_INFO("connect to {}", addr);
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        // 连接建立成功时，可能通道已经关闭，此时直接返回
        if (status_ == Status::kShutdown) {
            co_return;
        }
        status_ = Status::kConnected;
        callbacks_.clear();
        stream_ = std::move(has_stream.value());
        kosio::spawn(handle_response_loop());
        kosio::spawn(send_request_loop());
    }

    auto trigger_callback(uint64_t request_id, StatusCode code, std::string_view resp_payload) -> kosio::async::Task<void> {
        Callback callback;
        {
            CallbackMap::accessor acc;
            if (!callbacks_.find(acc, request_id)) {
                co_return;
            }
            callback = std::move(acc->second);
        }
        callbacks_.erase(request_id);
        co_await callback(code, resp_payload);
    }

private:
    auto handle_response_loop() -> kosio::async::Task<void> {
        std::vector<char> buf(detail::MAX_MESSAGE_SIZE);

        detail::ResponseHeader resp_header;
        while (true) {
            if (auto ret = co_await stream_.read_exact(
                {reinterpret_cast<char*>(&resp_header), sizeof(resp_header)}); !ret) {
                break;
            }

            auto request_id = be64toh(resp_header.request_id);
            auto payload_size = be32toh(resp_header.payload_size);
            auto code = resp_header.code;

            if (payload_size > vrpc::detail::MAX_MESSAGE_SIZE) {
                LOG_ERROR("receive unusual message");
                break;
            }

            if (payload_size > 0) {
                if (auto ret = co_await stream_.read_exact({buf.data(), payload_size}); !ret) {
                    LOG_ERROR("failed to receive response payload: {}", ret.error());
                    break;
                }
            }

            co_await trigger_callback(request_id, code, {buf.data(), payload_size});
        }
        LOG_INFO("connection to {}:{} closed", server_ip_, server_port_);
    }

    auto send_request_loop() -> kosio::async::Task<void> {
        detail::RequestHeader req_header;
        while (true) {
            auto has_request = co_await receiver_.recv();
            if (!has_request) {
                break;
            }
            auto request = std::move(has_request.value());
            auto& req_payload = request.req_payload;
            req_header.request_id = htobe64(request.request_id);
            req_header.payload_size = htobe32(req_payload.size());
            req_header.service_type = request.service_type;
            req_header.invoke_type = request.invoke_type;

            callbacks_.emplace(request.request_id, std::move(request.callback));

            auto ret = co_await stream_.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&req_header), sizeof(req_header)),
                std::span<const char>(req_payload.data(), req_payload.size())
            );

            if (!ret) {
                callbacks_.erase(request.request_id);
            }
        }
        latch_.count_down();
    }

private:
    std::string                      server_ip_;
    uint16_t                         server_port_;
    kosio::net::TcpStream            stream_;
    kosio::sync::Mutex               mutex_;
    kosio::sync::Latch               latch_{1};
    Status                           status_{Status::kDisconnected};
    detail::RequestChannel::Sender   sender_{nullptr};
    detail::RequestChannel::Receiver receiver_{nullptr};
    CallbackMap                      callbacks_;
};
} // namespace vrpc