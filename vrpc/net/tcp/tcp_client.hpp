#pragma once
#include <kosio/net.hpp>
#include "vrpc/net/detail/callback.hpp"
#include "vrpc/net/detail/config.hpp"
#include "vrpc/net/pb/detail/channel.hpp"

namespace vrpc {
using kosio::sync::Mutex;
using kosio::net::TcpStream;
using kosio::net::SocketAddr;
using kosio::async::Task;
class TcpClient {
    enum State {
        Idel,
        Connecting,
        Ready,
        TransientFailure,
        Shutdown,
    };

public:
    explicit TcpClient(detail::Config config)
        : config_(std::move(config))
        , stream_(kosio::net::detail::Socket{-1}) {
        auto has_addr = SocketAddr::parse(config_.ip, config_.port);
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
        return config_.ip;
    }

    [[nodiscard]]
    auto port() const -> uint16_t {
        return config_.port;
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
        const std::function<Task<void>(const vrpc::Status& status, const Resp& response)>& callback) -> Task<void> {
        auto message = detail::RpcRequestMessage::make(service_name, method_name, request.SerializeAsString());
        auto rpc_callback = std::make_unique<detail::RpcCallbackImpl<Resp>>(callback);
        if (message.bytes_size() > detail::MAX_RPC_MESSAGE_SIZE) {
            LOG_ERROR("vrpc request message too large");
            co_return;
        }

        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        if (status_ == Shutdown) {
            co_return;
        }

        auto seq = message.seq_;
        if (auto ret = co_await sender_->send(message); !ret) {
            LOG_ERROR("{}", ret.error());
            co_return;
        }
        callbacks_.emplace(seq, std::move(rpc_callback));
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> Task<void> {
        while (true) {
            co_await kosio::io::cancel(stream_.fd(), IORING_ASYNC_CANCEL_ALL);
            co_await kosio::time::sleep(detail::SHUT_DOWN_WAITING_INTERVAL);
        }
        LOG_INFO("vrpc tcp client closed");
    }

private:
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
    }

private:
    auto send_request_loop() -> Task<void> {
        detail::RpcMessageHeader header{};
        while (true) {
            auto has_request = co_await receiver_->recv();
            if (!has_request) {
                break;
            }
            auto request = std::move(has_request.value());
            request.htobe(); // 转为网络字节序
            header.msg_size = htobe32(request.bytes_size());

            if (auto status = co_await connect();!status.ok()) {
                kosio::spawn(trigger_callback(detail::RpcResponseMessage::make(be64toh(request.seq_), status)));
                continue;
            }

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
    }

    auto handle_response_loop() -> Task<void> {
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

            // 启动回调协程
            kosio::spawn(trigger_callback(std::move(message.value())));
        }
    }

private:
    [[REMEMBER_CO_AWAIT]]
    auto connect() -> Task<Status> {
        std::size_t retry_times{1};
        std::size_t block_off{static_cast<std::size_t>(detail::MIN_CONNECT_TIMEOUT)};
        co_await mutex_.lock();
        if (state_ == Shutdown) {
            mutex_.unlock();
            co_return Status{Status::kCancelled, "connection closed"};
        }

        if (state_ != Idel) {
            mutex_.unlock();
            co_return Status{Status::kOk};
        }

        state_ = Connecting;
        mutex_.unlock();
        while (retry_times < config_.max_connect_retry_times) {
            // TODO：存储多个服务器地址并尝试连接
            auto has_stream = co_await TcpStream::connect(peer_addr_).set_timeout(block_off);
            if (!has_stream) {
                co_await mutex_.lock();
                if (state_ == Shutdown) {
                    mutex_.unlock();
                    co_return Status{Status::kCancelled, "connection closed"};
                }
                state_ = TransientFailure;
                mutex_.unlock();

                block_off = get_block_off(retry_times++);
                co_await mutex_.lock();
                if (state_ == Shutdown) {
                    mutex_.unlock();
                    co_return Status{Status::kCancelled, "connection closed"};
                }
                state_ = Idel;
                mutex_.unlock();
                continue;
            }
            auto stream = std::move(has_stream.value());

            // set tcp nodelay
            if (auto ret = stream.set_nodelay(true); !ret) {
                LOG_ERROR("{}", ret.error());
                continue;
            }

            co_await mutex_.lock();
            // TODO：添加健康检查后再设置为 Ready
            if (state_ != Shutdown) {
                state_ = Ready;
            }
            stream_ = std::move(stream);
            mutex_.unlock();
            co_return Status{Status::kOk};
        }
        co_return Status{Status::kUnavailable, "connect failed"};
    }

    [[nodiscard]]
    auto get_block_off(std::size_t retry_times) const -> std::size_t {
        return std::min(
            config_.max_connect_timeout,
            static_cast<std::size_t>(1.6 * detail::MIN_CONNECT_TIMEOUT * static_cast<double>(retry_times)));
    }

private:
    detail::Config                config_;
    SocketAddr                    peer_addr_{};
    State                         state_{Idel};
    TcpStream                     stream_;
    Mutex                         mutex_;
    detail::RpcRequestSenderPtr   sender_{nullptr};
    detail::RpcRequestReceiverPtr receiver_{nullptr};
    detail::RpcCallbackMap        callbacks_;
};
} // namespace vrpc