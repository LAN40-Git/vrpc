#pragma once
#include <kosio/net.hpp>
#include "vrpc/net/detail/callback.hpp"
#include "vrpc/net/detail/config.hpp"
#include "vrpc/net/pb/detail/channel.hpp"

namespace vrpc {
class TcpClient {
    enum Status {
        kConnecting,
        kConnected,
        kDisconnected,
        kShuttingDown,
        kShutdown
    };

    [[nodiscard]]
    auto is_connecting() const -> bool {
        return status_ == kConnecting;
    }

    [[nodiscard]]
    auto is_connected() const -> bool {
        return status_ == kConnected;
    }

    [[nodiscard]]
    auto is_disconnected() const -> bool {
        return status_ == kDisconnected;
    }

    [[nodiscard]]
    auto is_shutting_down() const -> bool {
        return status_ == kShuttingDown;
    }

    [[nodiscard]]
    auto is_shutdown() const -> bool {
        return status_ == kShutdown;
    }

public:
    explicit TcpClient(std::string_view ip, uint16_t port)
        : ip_(ip)
        , port_(port)
        , stream_(kosio::net::detail::Socket{-1}) {
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

public:
    template <typename Req, typename Resp>
        requires std::is_base_of_v<google::protobuf::Message, Req> &&
                 std::is_base_of_v<google::protobuf::Message, Resp>
    void call_method(
        std::string_view service_name,
        std::string_view method_name,
        const Req& request,
        const std::function<kosio::async::Task<void>(const vrpc::Status& status, const Resp& response)>& callback) {
        kosio::spawn(register_callback(
            detail::RpcRequestMessage::make(service_name, method_name, request.SerializeAsString()),
            std::make_unique<detail::RpcCallbackImpl<Resp>>(callback)));
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> kosio::async::Task<void> {
        {
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            status_ = kShuttingDown;
            sender_->close();
        }

        while (true) {
            LOG_INFO("is waiting");
            co_await kosio::io::cancel(stream_.fd(), IORING_ASYNC_CANCEL_ALL);
            co_await kosio::time::sleep(detail::SHUT_DOWN_WAITING_INTERVAL);
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            if (is_shutdown() || is_disconnected()) {
                break;
            }
        }
        co_await latch_.wait();
    }

private:
    auto connect() -> kosio::async::Task<void> {
        // 重试次数
        std::size_t retry_times{1};
        while (true) {
            auto has_addr = kosio::net::SocketAddr::parse(ip_, port_);
            if (!has_addr) {
                LOG_ERROR("{}", has_addr.error());
                break;
            }
            auto peer_addr = has_addr.value();

            auto has_stream = co_await kosio::net::TcpStream::connect(peer_addr).set_timeout(max_connect_timeout_);
            if (!has_stream) {
                LOG_ERROR("{}", has_stream.error());
                co_await kosio::time::sleep(do_avoid(retry_times++));
                continue;
            }

            if (auto ret = has_stream.value().set_nodelay(true); !ret) {
                LOG_VERBOSE("{}", ret.error());
                continue;
            }

            LOG_INFO("vrpc tcp client connect to {}", peer_addr);
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            // 连接建立成功时，可能客户端正在或已经关闭，此时直接返回
            if (is_shutting_down() || is_shutdown()) {
                co_return;
            }
            status_ = kConnected;
            stream_ = std::move(has_stream.value());
            kosio::spawn(handle_response_loop());
            kosio::spawn(send_request_loop());
            co_return;
        }
    }

    [[nodiscard]]
    auto do_avoid(std::size_t retry_times) const -> std::size_t {
        return std::min(max_connect_timeout_,
            static_cast<std::size_t>(retry_times * 2 * 1000));
    }

    auto register_callback(detail::RpcRequestMessage message, detail::RpcCallback callback) -> kosio::async::Task<void> {
        // 校验大小
        if (message.bytes_size() > detail::MAX_RPC_MESSAGE_SIZE) {
            LOG_INFO("{}", message.bytes_size());
            co_return;
        }

        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};

        // 客户端关闭，RPC 服务不可用
        if (is_shutting_down() || is_shutdown()) {
            co_return;
        }

        // 连接未建立
        if (is_disconnected() && !is_connecting()) {
            // 若未启动连接建立协程，则启动
            status_ = kConnecting;
            kosio::spawn(connect());
        }

        // 缓存消息
        auto seq = message.seq_;
        if (auto ret = co_await sender_->send(message); !ret) {
            LOG_ERROR("{}", ret.error());
            co_return;
        }
        callbacks_.emplace(seq, std::move(callback));
    }

    auto trigger_callback(detail::RpcResponseMessage message) -> kosio::async::Task<void> {
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
    auto handle_response_loop() -> kosio::async::Task<void> {
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
                LOG_ERROR("rpc response message too large");
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
                LOG_ERROR("rpc response message parse failed");
                break;
            }

            // 启动回调协程
            kosio::spawn(trigger_callback(std::move(message.value())));
        }
        LOG_INFO("connection {}:{} closed", ip_, port_);

        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        if (is_shutting_down()) {
            status_ = kShutdown;
        }
        status_ = kDisconnected;
    }

    auto send_request_loop() -> kosio::async::Task<void> {
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
                kosio::spawn(trigger_callback(detail::RpcResponseMessage::make(
                    be64toh(request.seq_),
                    vrpc::Status::kUnavailable,
                    std::string{ret.error().message()},
                    "")));
            }
        }
        latch_.count_down();
    }

private:
    std::size_t                   max_connect_timeout_{8000}; // ms
    std::string                   ip_;
    uint16_t                      port_;
    kosio::net::TcpStream         stream_;
    detail::RpcRequestSenderPtr   sender_{nullptr};
    detail::RpcRequestReceiverPtr receiver_{nullptr};
    kosio::sync::Mutex            mutex_;
    kosio::sync::Latch            latch_{1};
    Status                        status_{kDisconnected};
    detail::RpcCallbackMap        callbacks_;
};
} // namespace vrpc