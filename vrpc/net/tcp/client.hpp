#pragma once
#include <kosio/net.hpp>
#include <kosio/sync.hpp>
#include <tbb/concurrent_hash_map.h>
#include "vrpc/common/concept.hpp"
#include "vrpc/net/pb/detail/channel.hpp"
#include "vrpc/net/builder.hpp"

namespace vrpc {
using RpcCallback = std::function<kosio::async::Task<void>(Status::Code, std::string_view)>;
class TcpClient {
    using RpcCallbackMap = tbb::concurrent_hash_map<uint64_t, RpcCallback>;

    enum class Status {
        kConnecting,
        kConnected,
        kDisconnected,
        kShuttingDown,
        kShutdown
    };

private:
    explicit TcpClient(const detail::Config& config)
        : config_(config)
        , stream_(kosio::net::detail::Socket{-1}) {
        auto [sender, receiver] =
                            detail::RpcRequestChannel::make(config_.channel_capacity);
        sender_ = std::make_shared<detail::RpcRequestSender>(std::move(sender));
        receiver_ = std::make_shared<detail::RpcRequestReceiver>(std::move(receiver));
    }

public:
    // Delete copy
    TcpClient(const TcpClient&) = delete;
    auto operator=(const TcpClient&) -> TcpClient& = delete;

    // Delete move
    TcpClient(TcpClient&&) = delete;
    auto operator=(TcpClient&&) -> TcpClient& = delete;

public:
    auto server_ip() const noexcept -> std::string_view {
        return config_.ip;
    }

    auto server_port() const noexcept -> uint16_t {
        return config_.port;
    }

public:
    template <ProtobufMessage P>
    [[REMEMBER_CO_AWAIT]]
    auto call(
        std::string&& service_name,
        std::string&& method_name,
        const P& request,
        const RpcCallback& callback) -> kosio::async::Task<vrpc::Status::Code> {
        // 构造报文
        detail::RpcRequestMessage message;
        message.set_service_name(std::move(service_name));
        message.set_method_name(std::move(method_name));
        message.set_payload(request.SerializeAsString());

        // 校验大小
        if (message.bytes_size() > detail::MAX_RPC_MESSAGE_SIZE) {
            co_return vrpc::Status::kResourceExhausted;
        }

        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        // 通道关闭，RPC 服务不可用
        if (status_ == Status::kShuttingDown ||
            status_ == Status::kShutdown) {
            co_return vrpc::Status::kUnavailable;
        }
        // 连接未建立
        if (status_ == Status::kDisconnected) {
            // 若未启动连接建立协程，则启动
            if (status_ != Status::kConnecting) {
                status_ = Status::kConnecting;
                kosio::spawn(connect());
            }
        }
        auto seq = message.seq;
        if (auto ret = co_await sender_->send(message); !ret) {
            LOG_ERROR("{}", ret.error());
            co_return vrpc::Status::kInternal;
        }
        callbacks_.emplace(seq, callback);
        co_return vrpc::Status::kOk;
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
            co_await kosio::time::sleep(config_.shutdown_waiting_interval);
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            if (status_ == Status::kShutdown) {
                break;
            }
        }
        co_await latch_.wait();
    }

private:
    [[nodiscard]]
    auto do_avoid(int retry_times) const -> std::size_t {
        return std::min(config_.max_connect_timeout,
            static_cast<std::size_t>(retry_times * 2 * 1000));
    }

    auto connect() -> kosio::async::Task<void> {
        // 重试次数
        std::size_t n{1};
        while (true) {
            auto has_addr = kosio::net::SocketAddr::parse(config_.ip, config_.port);
            if (!has_addr) {
                LOG_ERROR("{}", has_addr.error());
                break;
            }
            auto addr = has_addr.value();

            auto has_stream = co_await kosio::net::TcpStream::connect(addr).set_timeout(config_.max_connect_timeout);
            if (!has_stream) {
                LOG_VERBOSE("{}", has_stream.error());
                co_await kosio::time::sleep(do_avoid(n++));
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

    auto trigger_callback(uint64_t request_id, vrpc::Status::Code code, std::string_view resp_payload) -> kosio::async::Task<void> {
        RpcCallback callback;
        {
            RpcCallbackMap::accessor acc;
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
        std::vector<char> buf(detail::MAX_RPC_MESSAGE_SIZE);
        detail::RpcMessageHeader header;
        while (true) {
            // 读取报文头
            if (auto ret = co_await stream_.read_exact(
                std::span<char>(reinterpret_cast<char*>(&header), sizeof(header))); !ret) {
                break;
            }

            auto msg_size = be32toh(header.msg_size);

            // 读取报文


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
                co_await trigger_callback(request_id, StatusCode::kUnknown, "");
            }
        }
        latch_.count_down();
    }

private:
    using RpcSenderPtr = std::shared_ptr<detail::RpcSender>;
    using RpcReceiverPtr = std::shared_ptr<detail::RpcReceiver>;

    detail::Config        config_;
    kosio::net::TcpStream stream_;
    kosio::sync::Mutex    mutex_;
    kosio::sync::Latch    latch_{1};
    Status                status_{Status::kDisconnected};
    RpcSenderPtr          sender_{nullptr};
    RpcReceiverPtr        receiver_{nullptr};
    RpcCallbackMap        callbacks_;
};
} // namespace vrpc