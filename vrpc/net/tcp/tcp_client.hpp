#pragma once
#include <kosio/net.hpp>
#include <kosio/signal/signal.hpp>

#include "vrpc/net/detail/callback.hpp"
#include "vrpc/net/detail/config.hpp"
#include "vrpc/net/pb/detail/channel.hpp"
#include "vrpc/common/error.hpp"

namespace vrpc {
using kosio::sync::Mutex;
using kosio::net::TcpStream;
using kosio::net::SocketAddr;
using kosio::async::Task;
class TcpClient {
    enum State {
        Connecting,
        Ready,
        Disconnecting,
        Disconnected,
        Shutdown,
    };

public:
    explicit TcpClient(std::string_view ip, uint16_t port)
        : ip_(ip)
        , port_(port)
        , stream_(kosio::net::detail::Socket{-1}) {
        callbacks_.rehash(detail::CHANNEL_CAPACITY);
        auto has_addr = SocketAddr::parse(ip, port);
        if (!has_addr) {
            LOG_ERROR("{}", has_addr.error());
        } else {
            peer_addr_ = has_addr.value();
        }

        auto [sender, receiver] =
                            detail::RpcRequestChannel::make(256);
        sender_ = std::make_shared<detail::RpcRequestChannel::Sender>(std::move(sender));
        receiver_ = std::make_shared<detail::RpcRequestChannel::Receiver>(std::move(receiver));
    }

    // Delete copy
    TcpClient(const TcpClient&) = delete;
    auto operator=(const TcpClient&) -> TcpClient& = delete;

    // Delete move
    TcpClient(TcpClient&&) = delete;
    auto operator=(TcpClient&&) -> TcpClient& = delete;

public:
    [[nodiscard]]
    auto ip() const -> std::string_view {
        return ip_;
    }

    [[nodiscard]]
    auto port() const -> uint16_t {
        return port_;
    }

    [[nodiscard]]
    auto peer_addr() const -> SocketAddr {
        return peer_addr_;
    }

public:
    template <typename Req, typename Resp>
        requires std::is_base_of_v<google::protobuf::Message, Req> &&
                 std::is_base_of_v<google::protobuf::Message, Resp>
    [[REMEMBER_CO_AWAIT]]
    auto call_method(
        std::string_view service_name,
        std::string_view method_name,
        const Req& request,
        const std::function<Task<void>(const Status& status, const Resp& response)>& callback) -> Task<void> {
        std::call_once(once_flag_, [this](){
            kosio::spawn(register_shutdown_signal());
        });

        auto message = detail::RpcRequestMessage::make(service_name, method_name, request.SerializeAsString());
        auto rpc_callback = std::make_unique<detail::RpcCallbackImpl<Resp>>(callback);
        if (message.bytes_size() > detail::MAX_RPC_MESSAGE_SIZE) {
            LOG_ERROR("vrpc request message too large");
            co_return;
        }

        co_await mutex_.lock();
        co_await channel_mutex_.lock();
        if (state_ == Shutdown) {
            mutex_.unlock();
            channel_mutex_.unlock();
            co_return;
        }

        if (state_ == Disconnected) {
            state_ = Connecting;
            coro_tasks_.fetch_add(1, std::memory_order_relaxed);
            kosio::spawn(connect_loop());
        }
        mutex_.unlock();

        auto seq = message.seq_;
        if (auto ret = co_await sender_->send(message); !ret) {
            LOG_ERROR("{}", ret.error());
            channel_mutex_.unlock();
            co_return;
        }
        channel_mutex_.unlock();
        callbacks_.emplace(seq, std::move(rpc_callback));
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> Task<void> {
        co_await mutex_.lock();
        co_await channel_mutex_.lock();
        if (state_ == Shutdown) {
            mutex_.unlock();
            channel_mutex_.unlock();
            co_return;
        }
        state_ = Shutdown;
        mutex_.unlock();

        sender_->close();
        channel_mutex_.unlock();

        while (coro_tasks_.load(std::memory_order_relaxed) > 0) {
            co_await kosio::io::cancel(stream_.fd(), IORING_ASYNC_CANCEL_ALL);
            co_await kosio::time::sleep(detail::SHUT_DOWN_WAITING_INTERVAL);
        }
        LOG_INFO("vrpc tcp client closed");
    }

private:
    auto register_shutdown_signal() -> Task<void> {
        co_await kosio::signal::ctrl_c();
        co_await shutdown();
    }

    auto trigger_callback(detail::RpcResponseMessage message) -> Task<void> {
        detail::RpcCallback callback;
        {
            detail::RpcCallbackMap::accessor acc;
            if (!callbacks_.find(acc, message.seq_)) {
                co_return;
            }
            callback = std::move(acc->second);
        }
        callbacks_.erase(message.seq_);
        co_await callback->run(message);
        coro_tasks_.fetch_sub(1, std::memory_order_relaxed);
    }

private:
    auto send_request_loop() -> Task<void> {
        coro_tasks_.fetch_add(1, std::memory_order_relaxed);
        detail::RpcMessageHeader header{};
        while (true) {
            auto has_request = co_await receiver_->recv();
            if (!has_request) {
                break;
            }
            auto request = std::move(has_request.value());
            request.htobe(); // 转为网络字节序
            header.msg_size = htobe32(request.bytes_size());

            auto ret = co_await stream_.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&header), sizeof(header)),
                std::span<const char>(reinterpret_cast<char*>(&request.seq_), sizeof(request.seq_)),
                std::span<const char>(reinterpret_cast<char*>(&request.service_name_size_), sizeof(request.service_name_size_)),
                request.service_name_,
                std::span<const char>(reinterpret_cast<char*>(&request.method_name_size_), sizeof(request.method_name_size_)),
                request.method_name_,
                std::span<const char>(reinterpret_cast<char*>(&request.payload_size_), sizeof(request.payload_size_)),
                request.payload_,
                std::span<const char>(reinterpret_cast<char*>(&request.check_sum_), sizeof(request.check_sum_))
            );

            if (!ret) {
                LOG_ERROR("{}", ret.error());
                kosio::spawn(trigger_callback(detail::RpcResponseMessage::make(
                    be64toh(request.seq_),
                    Status::kUnavailable, "send rpc request message failed")));
            }
        }
        co_await mutex_.lock();
        switch (state_) {
            case Shutdown:
                break;
            case Disconnecting:
                state_ = Disconnected;
                co_await stream_.close();
                break;
            default:
                break;
        }
        mutex_.unlock();
        coro_tasks_.fetch_sub(1, std::memory_order_relaxed);
    }

    auto handle_response_loop() -> Task<void> {
        coro_tasks_.fetch_add(1, std::memory_order_relaxed);
        std::vector<char> buf(detail::MAX_RPC_MESSAGE_SIZE);
        detail::RpcMessageHeader header;
        while (true) {
            // 读取回复报文头
            if (auto ret = co_await stream_.read_exact(
                std::span<char>(reinterpret_cast<char*>(&header), sizeof(header))); !ret) {
                break;
            }

            // 校验消息大小
            auto msg_size = be32toh(header.msg_size);
            if (msg_size > detail::MAX_RPC_MESSAGE_SIZE) {
                LOG_ERROR("vrpc response message too large");
                break;
            }

            // 读取回复报文
            if (auto ret = co_await stream_.read_exact(
            std::span<char>(buf.data(), msg_size)); !ret) {
                break;
            }

            // 解析回复报文
            auto message = detail::RpcResponseMessage::parse_from(buf.data(), msg_size);
            if (!message) {
                LOG_ERROR("vrpc response message parse failed");
                break;
            }

            coro_tasks_.fetch_add(1, std::memory_order_relaxed);
            // 启动回调协程
            kosio::spawn(trigger_callback(std::move(message.value())));
        }
        co_await mutex_.lock();
        switch (state_) {
            case Shutdown:
                break;
            case Disconnecting:
                state_ = Disconnected;
                co_await stream_.close();
                break;
            default:
                break;
        }
        mutex_.unlock();
        coro_tasks_.fetch_sub(1, std::memory_order_relaxed);
    }

private:
    [[REMEMBER_CO_AWAIT]]
    auto connect_loop() -> Task<void> {
        auto retry_times{1};
        auto backoff{detail::BASE_DELAY};
        while (true) {
            co_await mutex_.lock();
            if (state_ == Shutdown) {
                mutex_.unlock();
                break;
            }
            mutex_.unlock();

            auto has_stream = co_await TcpStream::connect(peer_addr_).set_timeout(std::max(backoff, detail::MIN_CONNECT_TIMEOUT));
            if (!has_stream) {
                LOG_ERROR("{}", has_stream.error());
                backoff = get_block_off(retry_times++);
                LOG_INFO("backoff for {}", backoff);
                co_await kosio::time::sleep(backoff);
                continue;
            }
            auto stream = std::move(has_stream.value());

            if (auto ret = stream.set_nodelay(true); !ret) {
                LOG_ERROR("{}", ret.error());
                backoff = get_block_off(retry_times++);
                LOG_INFO("backoff for {}", backoff);
                co_await kosio::time::sleep(backoff);
                continue;
            }

            co_await mutex_.lock();
            if (state_ == Shutdown) {
                mutex_.unlock();
                break;
            }
            state_ = Ready;
            LOG_INFO("connect to {}", peer_addr_);
            stream_ = std::move(stream);
            kosio::spawn(send_request_loop());
            kosio::spawn(handle_response_loop());
            mutex_.unlock();
            break;
        }
        coro_tasks_.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]]
    static auto get_block_off(std::size_t retry_times) -> std::size_t {
        return std::min(
            detail::MAX_BACKOFF,
            static_cast<std::size_t>(
                detail::MULTIPLIER *
                static_cast<double>(detail::BASE_DELAY) *
                static_cast<double>(retry_times)));
    }

private:
    std::once_flag                once_flag_;
    std::atomic<int>              coro_tasks_{0};
    std::string                   ip_;
    uint16_t                      port_;
    SocketAddr                    peer_addr_{};
    State                         state_{Disconnected};
    TcpStream                     stream_;
    Mutex                         mutex_;
    Mutex                         channel_mutex_;
    detail::RpcRequestSenderPtr   sender_{nullptr};
    detail::RpcRequestReceiverPtr receiver_{nullptr};
    detail::RpcCallbackMap        callbacks_;
};
} // namespace vrpc