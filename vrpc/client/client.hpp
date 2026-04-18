#pragma once
#include <kosio/net.hpp>
#include <kosio/sync.hpp>
#include "vrpc/core/detail/config.hpp"
#include "vrpc/core/detail/request.hpp"

namespace vrpc::client {
namespace detail {
class Connection {
public:
    explicit Connection(
        const kosio::net::SocketAddr& addr,
        kosio::net::TcpStream stream)
        : addr_(addr)
        , stream_(std::move(stream))
        , resp_buf_(vrpc::detail::MAX_RPC_MESSAGE_SIZE) {}

public:
    kosio::net::SocketAddr addr_;
    kosio::net::TcpStream  stream_;
    std::vector<char>      resp_buf_;
    std::atomic<bool>      is_running_{true};
};
} // namespace detail

class RpcClient {
    enum class Status {
        kConnecting = 0,
        kConnected,
        kDisconnecting,
        kDisconnected,
        kStopping,
        kStopped
    };

    using RpcCallbackMap = std::unordered_map<uint64_t, RpcCallback>;

public:
    explicit RpcClient(std::string_view server_ip, uint16_t server_port)
        : server_ip_(server_ip), server_port_(server_port) {}

    // Delete copy
    RpcClient(const RpcClient&) = delete;
    auto operator=(const RpcClient&) -> RpcClient& = delete;

    // Delete move
    RpcClient(RpcClient&&) = delete;
    auto operator=(RpcClient&&) -> RpcClient& = delete;

public:
    template <typename Request>
    [[REMEMBER_CO_AWAIT]]
    auto call(
        Type service_type,
        Type invoke_type,
        const Request& request,
        const RpcCallback& callback) -> kosio::async::Task<void> {
        static std::atomic<uint64_t> current_request_id{0};
        auto request_id = current_request_id.fetch_add(1);
        auto req_payload = request.SerializeAsString();
        if (req_payload.size() > vrpc::detail::MAX_RPC_MESSAGE_SIZE) {
            co_return;
        }
        auto reqeust = vrpc::detail::RpcRequest{request_id, service_type, invoke_type, std::move(req_payload), callback};

        co_await mutex_.lock();
        // 若正在建立连接，则直接返回
        if (status_ == Status::kConnecting) {
            co_return;
        }

        // 若未建立连接，则尝试建立连接
        if (status_ == Status::kDisconnected) {
            status_ = Status::kConnecting;
            kosio::spawn(connect());
            // 首次等待一段时间
            co_await kosio::time::sleep(vrpc::detail::CONNECT_TIMEOUT_MS);
            // 若还未建立连接，则
            if (status_ == Status::kDisconnected) {
                co_return;
            }
        }

        if (status_ == Status::kStopping ||
            status_ == Status::kStopped) {
            co_return;
        }

        co_await sender_.send(reqeust);
    }

    [[REMEMBER_CO_AWAIT]]
    auto stop() -> kosio::async::Task<void> {
        {
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            // 若未连接到服务端，则直接设置为停止
            if (status_ == Status::kDisconnected) {
                status_ = Status::kStopped;
                co_return;
            }

            // 若已经停止，则直接返回
            if (status_ == Status::kStopped) {
                co_return;
            }

            // 设置为正在停止
            status_ = Status::kStopping;
            // 关闭通道
            sender_.close();
        }

        // 等待循环结束
        while (true) {
            co_await kosio::time::sleep(vrpc::detail::WAITING_INTERVAL_MS);
            co_await mutex_.lock();
            std::lock_guard lock{mutex_, std::adopt_lock};
            if (status_ == Status::kStopped) {
                break;
            }

        }

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
        auto stream = std::move(has_stream.value());

        if (auto ret = stream.set_nodelay(true); !ret) {
            LOG_ERROR("{}", ret.error());
            co_return;
        }

        LOG_INFO("connect to {}", addr);
        status_ = Status::kConnected;
        auto [sender, receiver] =
                            vrpc::detail::RpcRequestChannel::make(vrpc::detail::RPC_REQUEST_CHANNEL_CAPACITY);
        sender_ = std::move(sender);
        receiver_ = std::move(receiver);

        auto conn = std::make_shared<detail::Connection>(addr, std::move(stream));
        kosio::spawn(handle_response_loop(conn));
        kosio::spawn(send_request_loop(conn));
    }

    auto trigger_callback(uint64_t request_id, StatusCode code, std::string_view resp_payload) -> kosio::async::Task<void> {
        co_await callback_map_mutex_.lock();
        auto it = callback_map_.find(request_id);
        if (it == callback_map_.end()) {
            callback_map_mutex_.unlock();
            co_return;
        }
        auto callback = std::move(it->second);
        callback_map_.erase(request_id);
        callback_map_mutex_.unlock();
        co_await callback(code, resp_payload);
    }

private:
    auto handle_response_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& buf = conn->resp_buf_;

        vrpc::detail::RpcResponseHeader resp_header;
        while (true) {
            if (auto ret = co_await stream.read_exact(
                {reinterpret_cast<char*>(&resp_header), sizeof(resp_header)}); !ret) {
                break;
            }

            auto request_id = be64toh(resp_header.request_id);
            auto payload_size = be32toh(resp_header.payload_size);
            auto code = resp_header.code;

            if (payload_size > vrpc::detail::MAX_RPC_MESSAGE_SIZE) {
                LOG_ERROR("receive unusual message");
                break;
            }

            if (payload_size > 0) {
                if (auto ret = co_await stream.read_exact({buf.data(), payload_size}); !ret) {
                    LOG_ERROR("failed to receive response payload: {}", ret.error());
                    break;
                }
            }

            co_await trigger_callback(request_id, code, {buf.data(), payload_size});
        }
        conn->is_running_.store(false, std::memory_order_release);
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        if (status_ == Status::kConnected) {
            sender_.close();
        }
    }

    auto send_request_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;

        vrpc::detail::RpcRequestHeader req_header;
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

            co_await callback_map_mutex_.lock();
            callback_map_.emplace(request.request_id, std::move(request.callback));
            callback_map_mutex_.unlock();

            auto ret = co_await stream.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&req_header), sizeof(req_header)),
                std::span<const char>(req_payload.data(), req_payload.size())
            );

            if (!ret) {
                co_await callback_map_mutex_.lock();
                callback_map_.erase(request.request_id);
                callback_map_mutex_.unlock();
                LOG_ERROR("failed to send rpc request: {}", ret.error());
            }
        }

        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
    }

private:
    std::string                      server_ip_;
    uint16_t                         server_port_;
    kosio::sync::Mutex               mutex_;
    Status                           status_{Status::kDisconnected};
    vrpc::detail::RpcRequestSender   sender_{nullptr};
    vrpc::detail::RpcRequestReceiver receiver_{nullptr};
    RpcCallbackMap                   callback_map_;
    kosio::sync::Mutex               callback_map_mutex_;
};
} // namespace vrpc::client