#pragma once
#include <google/protobuf/message.h>
#include <kosio/net.hpp>
#include <kosio/sync.hpp>
#include <tbb/concurrent_hash_map.h>
#include "vrpc/common/status.hpp"
#include "vrpc/net/pb/detail/message.hpp"

namespace detail {
class TcpClientStatus {

};
} // namespace

namespace vrpc {
using RpcCallback = std::function<kosio::async::Task<void>(Status::Code, std::string_view)>;
class TcpClient {
    using RpcCallbackMap = tbb::concurrent_hash_map<uint64_t, RpcCallback>;
    using RpcRequestChannel = kosio::sync::Channel<detail::RpcRequestMessage>;
    using SenderPtr = std::shared_ptr<RpcRequestChannel::Sender>;
    using ReceiverPtr = std::shared_ptr<RpcRequestChannel::Receiver>;

    enum class Status {
        kConnecting,
        kConnected,
        kDisconnected,
        kShuttingDown,
        kShutdown
    };

public:
    explicit TcpClient(const kosio::net::SocketAddr& addr)
        : peer_addr_(addr)
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
    auto peer_addr() const -> kosio::net::SocketAddr {
        return peer_addr_;
    }

public:
    template <typename T>
        requires std::is_base_of_v<google::protobuf::Message, T>
    [[REMEMBER_CO_AWAIT]]
    auto call(
        std::string_view service_name,
        std::string_view method_name,
        const T& request,
        const RpcCallback& callback) -> kosio::async::Task<void> {
        // 构造报文
        detail::RpcRequestMessage message(service_name, method_name, request.SerializeAsString());

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
        auto seq = message.seq_;
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
        }

        while (true) {
            co_await kosio::io::cancel(stream_.fd(), IORING_ASYNC_CANCEL_ALL);
            co_await kosio::time::sleep(shutdown_waiting_interval_);
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
        return std::min(max_connect_timeout_,
            static_cast<std::size_t>(retry_times * 2 * 1000));
    }

    auto connect() -> kosio::async::Task<void> {
        // 重试次数
        std::size_t n{1};
        while (true) {
            auto has_stream = co_await kosio::net::TcpStream::connect(peer_addr_).set_timeout(max_connect_timeout_);
            if (!has_stream) {
                LOG_VERBOSE("{}", has_stream.error());
                co_await kosio::time::sleep(do_avoid(n++));
                continue;
            }

            if (auto ret = has_stream.value().set_nodelay(true); !ret) {
                LOG_VERBOSE("{}", ret.error());
                continue;
            }

            LOG_INFO("connect to {}", peer_addr_);
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

    auto trigger_callback(detail::RpcResponseMessage response) -> kosio::async::Task<void> {
        RpcCallback callback;
        {
            RpcCallbackMap::accessor acc;
            if (!callbacks_.find(acc, response.seq_)) {
                co_return;
            }
            callback = std::move(acc->second);
        }
        callbacks_.erase(response.seq_);
        co_await callback(static_cast<vrpc::Status::Code>(response.status_code_), response.payload_);
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
            if (msg_size > detail::MAX_RPC_MESSAGE_SIZE) {
                LOG_ERROR("rpc response message too large");
                break;
            }

            // 读取报文
            if (auto ret = co_await stream_.read_exact(
            std::span<char>(buf.data(), msg_size)); !ret) {
                break;
            }

            // 解析报文
            detail::RpcResponseMessage response;
            if (!response.parse_from(buf.data(), msg_size)) {
                LOG_ERROR("rpc response message parse failed");
                break;
            }

            // 启动回调协程
            kosio::spawn(trigger_callback(std::move(response)));
        }
        LOG_INFO("connection {} closed", peer_addr_);
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        if (status_ != Status::kShuttingDown) {
            status_ = Status::kDisconnected;
        } else {
            status_ = Status::kShutdown;
        }
    }

    auto send_request_loop() -> kosio::async::Task<void> {
        detail::RpcMessageHeader header;
        while (true) {
            auto has_message = co_await receiver_->recv();
            if (!has_message) {
                break;
            }
            auto message = std::move(has_message.value());
            auto seq = message.seq_;
            message.htobe(); // 转为网络字节序

            co_await mutex_.lock();
            auto ret = co_await stream_.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&message.seq_), sizeof(message.seq_)),
                std::span<const char>(reinterpret_cast<char*>(&message.service_name_size_), sizeof(message.service_name_size_)),
                message.service_name_,
                std::span<const char>(reinterpret_cast<char*>(&message.method_name_size_), sizeof(message.method_name_size_)),
                message.method_name_,
                std::span<const char>(reinterpret_cast<char*>(&message.payload_size_), sizeof(message.payload_size_)),
                message.payload_,
                std::span<const char>(reinterpret_cast<char*>(&message.check_sum_), sizeof(message.check_sum_))
            );
            mutex_.unlock();

            if (!ret) {
                detail::RpcResponseMessage response;
                response.seq_ = seq;
                response.status_code_ = vrpc::Status::kInternal;
                response.err_msg_ = ret.error().message();
                kosio::spawn(trigger_callback(response));
            }
        }
        latch_.count_down();
    }

private:
    std::size_t            max_connect_timeout_{8000};    // ms
    std::size_t            shutdown_waiting_interval_{50}; // ms
    kosio::net::SocketAddr peer_addr_;
    kosio::net::TcpStream  stream_;
    SenderPtr              sender_{nullptr};
    ReceiverPtr            receiver_{nullptr};
    kosio::sync::Mutex     mutex_;
    kosio::sync::Latch     latch_{1};
    Status                 status_{Status::kDisconnected};
    RpcCallbackMap         callbacks_;
};
} // namespace vrpc