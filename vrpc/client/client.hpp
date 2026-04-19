#pragma once
#include <kosio/net.hpp>
#include <kosio/sync.hpp>
#include <tbb/concurrent_hash_map.h>
#include "vrpc/common/concept.hpp"
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
        kShuttingDown,
        kShutdown
    };

public:
    explicit Client(std::string_view server_ip, uint16_t server_port)
        : server_ip_(server_ip)
        , server_port_(server_port)
        , stream_(kosio::net::detail::Socket{-1}) {
        auto [sender, receiver] =
                            detail::RequestChannel::make(detail::REQUEST_CHANNEL_CAPACITY);
        sender_ = std::make_shared<detail::RequestSender>(std::move(sender));
        receiver_ = std::make_shared<detail::RequestReceiver>(std::move(receiver));
    }

    // Delete copy
    Client(const Client&) = delete;
    auto operator=(const Client&) -> Client& = delete;

    // Delete move
    Client(Client&&) = delete;
    auto operator=(Client&&) -> Client& = delete;

public:
    template <Proto P, VrpcType S, VrpcType I>
    [[REMEMBER_CO_AWAIT]]
    auto call(
        S service_type,
        I invoke_type,
        const P& request,
        const Callback& callback) -> kosio::async::Task<StatusCode> {
        auto request_id = current_request_id_.fetch_add(1);
        auto req_payload = request.SerializeAsString();
        if (req_payload.size() > detail::MAX_MESSAGE_SIZE) {
            co_return StatusCode::kResourceExhausted;
        }
        auto reqeust = detail::Request{
            request_id,
            static_cast<Type>(service_type),
            static_cast<Type>(invoke_type),
            std::move(req_payload),
            callback};

        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        // 通道关闭，RPC 服务不可用
        if (status_ == Status::kShuttingDown ||
            status_ == Status::kShutdown) {
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
        if (auto ret = co_await sender_->send(reqeust); !ret) {
            LOG_ERROR("{}", ret.error());
        }
        co_return StatusCode::kOk;
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> kosio::async::Task<void> {
        {
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            status_ = Status::kShuttingDown;
            sender_->close();
        }

        while (true) {
            co_await kosio::io::cancel(stream_.fd(), IORING_ASYNC_CANCEL_ALL);
            co_await kosio::time::sleep(detail::WAITING_INTERVAL_MS);
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            if (status_ == Status::kShutdown) {
                break;
            }
        }
        co_await latch_.wait();
    }

private:
    static auto do_avoid(int retry_times) noexcept -> std::size_t {
        return std::min(detail::MAX_CONNECT_BLOCK_OFF,
            retry_times * detail::CONNECT_MULTIPLIER * detail::INITIAL_CONNECT_BLOCK_OFF);
    }

    auto connect() -> kosio::async::Task<void> {
        // 重试次数
        std::size_t n{0};
        while (true) {
            ++n;
            auto has_addr = kosio::net::SocketAddr::parse(server_ip_, server_port_);
            if (!has_addr) {
                LOG_ERROR("{}", has_addr.error());
                break;
            }
            auto addr = has_addr.value();

            auto has_stream = co_await kosio::net::TcpStream::connect(addr);
            if (!has_stream) {
                LOG_VERBOSE("{}", has_stream.error());
                // 避让
                co_await kosio::time::sleep(do_avoid(n));
                continue;
            }

            if (auto ret = has_stream.value().set_nodelay(true); !ret) {
                LOG_VERBOSE("{}", ret.error());
                continue;
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
            co_return;
        }
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

            if (payload_size > detail::MAX_MESSAGE_SIZE) {
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
        LOG_INFO("connection on {}:{} closed", server_ip_, server_port_);
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        if (status_ != Status::kShuttingDown) {
            status_ = Status::kDisconnected;
        } else {
            status_ = Status::kShutdown;
        }
    }

    auto send_request_loop() -> kosio::async::Task<void> {
        detail::RequestHeader req_header;
        while (true) {
            auto has_request = co_await receiver_->recv();
            if (!has_request) {
                break;
            }
            auto request = std::move(has_request.value());
            auto request_id = request.request_id;
            auto& req_payload = request.req_payload;
            req_header.request_id = htobe64(request_id);
            req_header.payload_size = htobe32(req_payload.size());
            req_header.service_type = request.service_type;
            req_header.invoke_type = request.invoke_type;

            callbacks_.emplace(request.request_id, std::move(request.callback));

            auto ret = co_await stream_.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&req_header), sizeof(req_header)),
                std::span<const char>(req_payload.data(), req_payload.size())
            );

            if (!ret) {
                co_await trigger_callback(request_id, StatusCode::kInternal, "");
            }
        }
        latch_.count_down();
    }

private:
    using RequestSenderPtr = std::shared_ptr<detail::RequestSender>;
    using RequestReceiverPtr = std::shared_ptr<detail::RequestReceiver>;

    std::string           server_ip_;
    uint16_t              server_port_;
    kosio::net::TcpStream stream_;
    kosio::sync::Mutex    mutex_;
    kosio::sync::Latch    latch_{1};
    std::atomic<uint64_t> current_request_id_{0};
    Status                status_{Status::kDisconnected};
    RequestSenderPtr      sender_{nullptr};
    RequestReceiverPtr    receiver_{nullptr};
    CallbackMap           callbacks_;
};
} // namespace vrpc