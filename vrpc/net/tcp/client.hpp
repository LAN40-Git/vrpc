#pragma once
#include <google/protobuf/message.h>
#include <kosio/net.hpp>
#include <tbb/concurrent_hash_map.h>
#include "vrpc/net/pb/status.hpp"
#include "vrpc/net/detail/config.hpp"
#include "vrpc/net/pb/detail/channel.hpp"

namespace detail {
class TcpClientStatus {

};
} // namespace

namespace vrpc {
using RpcCallback = std::function<kosio::async::Task<void>(const RpcResponseMessage& response)>;
class TcpClient {
    using RpcCallbackMap = tbb::concurrent_hash_map<uint64_t, RpcCallback>;
    using RpcRequestChannel = kosio::sync::Channel<RpcRequestMessage>;
    using RpcRequestSender = RpcRequestChannel::Sender;
    using RpcRequestReceiver = RpcRequestChannel::Receiver;
    using RpcRequestSenderPtr = std::shared_ptr<RpcRequestChannel::Sender>;
    using RpcRequestReceiverPtr = std::shared_ptr<RpcRequestChannel::Receiver>;

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
                            RpcRequestChannel::make(256);
        sender_ = std::make_shared<RpcRequestChannel::Sender>(std::move(sender));
        receiver_ = std::make_shared<RpcRequestChannel::Receiver>(std::move(receiver));
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
    template <typename T>
        requires std::is_base_of_v<google::protobuf::Message, T>
    void call(
        std::string_view service_name,
        std::string_view method_name,
        const T& request,
        const RpcCallback& callback) {
        // 构造报文
        RpcRequestMessage message(service_name, method_name, request.SerializeAsString());
        kosio::spawn([this, message = std::move(message), callback]() mutable -> kosio::async::Task<void> {
            // 校验大小
            if (message.bytes_size() > detail::MAX_RPC_MESSAGE_SIZE) {
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
        });
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
            co_await kosio::io::cancel(stream_.fd(), IORING_ASYNC_CANCEL_ALL);
            co_await kosio::time::sleep(detail::SHUT_DOWN_WAITING_INTERVAL);
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            if (is_shutdown()) {
                break;
            }
        }
        co_await latch_.wait();
    }

private:
    [[nodiscard]]
    auto do_avoid(int retry_times) const -> std::size_t {
        return std::min(max_connect_timeout_,
            static_cast<std::size_t>(retry_times * 2 * 1000));
    }

    auto connect() -> kosio::async::Task<void> {
        // 重试次数
        std::size_t n{1};
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
                co_await kosio::time::sleep(do_avoid(n++));
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

    auto trigger_callback(RpcResponseMessage response) -> kosio::async::Task<void> {
        RpcCallback callback;
        {
            RpcCallbackMap::accessor acc;
            if (!callbacks_.find(acc, response.seq_)) {
                co_return;
            }
            callback = std::move(acc->second);
        }
        callbacks_.erase(response.seq_);
        co_await callback(response);
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
            auto response = RpcResponseMessage::parse_from(buf.data(), msg_size);
            if (!response) {
                LOG_ERROR("rpc response message parse failed");
                break;
            }

            // 启动回调协程
            kosio::spawn(trigger_callback(std::move(response.value())));
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
        detail::RpcMessageHeader header;
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
                RpcResponseMessage response{be64toh(request.seq_), vrpc::Status::kUnavailable, ret.error().message(), ""};
                kosio::spawn(trigger_callback(std::move(response)));
            }
        }
        latch_.count_down();
    }

private:
    std::size_t            max_connect_timeout_{8000}; // ms
    std::string            ip_;
    uint16_t               port_;
    kosio::net::TcpStream  stream_;
    RpcRequestSenderPtr    sender_{nullptr};
    RpcRequestReceiverPtr  receiver_{nullptr};
    kosio::sync::Mutex     mutex_;
    kosio::sync::Latch     latch_{1};
    Status                 status_{kDisconnected};
    RpcCallbackMap         callbacks_;
};
} // namespace vrpc