#pragma once
#include <utility>
#include <kosio/signal/signal.hpp>
#include "vrpc/net/detail/builder.hpp"
#include "vrpc/net/detail/method.hpp"
#include "vrpc/net/tcp/detail/server_cache.hpp"
#include "vrpc/net/tcp/detail/manager.hpp"

namespace vrpc {
class TcpServer {
public:
    explicit TcpServer(detail::Config config)
        : config_(std::move(config)) {
        auto has_addr = kosio::net::SocketAddr::parse(config_.host, config_.port);
        if (!has_addr) {
            LOG_ERROR("{}", has_addr.value());
        } else {
            addr_ = has_addr.value();
        }
    }

    // Delete copy
    TcpServer(const TcpServer&) = delete;
    auto operator=(const TcpServer&) -> TcpServer& = delete;

    // Delete move
    TcpServer(TcpServer&&) = delete;
    auto operator=(TcpServer&&) -> TcpServer& = delete;

public:
    auto port() const -> uint16_t {
        return config_.port;
    }

public:
    template <typename Req, typename Resp>
        requires std::is_base_of_v<google::protobuf::Message, Req> &&
                 std::is_base_of_v<google::protobuf::Message, Resp>
    auto register_method(
        const std::string& service_name,
        const std::string& method_name,
        const std::function<kosio::async::Task<Resp>(const Req& request)>& method) -> TcpServer& {
        services_[service_name][method_name] = std::make_unique<detail::RpcMethodImpl<Req, Resp>>(method);
        return *this;
    }

public:
    void wait() {
        kosio::runtime::MultiThreadBuilder::options()
            .set_num_workers(config_.thread_nums)
            .build()
            .block_on(accept_loop());
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> kosio::async::Task<void> {
        is_shutdown_.store(true, std::memory_order_release);
        while (is_accepting_.load(std::memory_order_acquire)) {
            co_await kosio::net::TcpStream::connect(addr_);
            co_await kosio::time::sleep(detail::SHUT_DOWN_WAITING_INTERVAL);
        }

        while (!manager_.empty()) {
            co_await manager_.cancel_all();
            co_await kosio::time::sleep(detail::SHUT_DOWN_WAITING_INTERVAL);
        }
        LOG_INFO("vrpc server on {} closed", addr_);
    }

private:
    auto accept_loop() -> kosio::async::Task<void> {
        auto has_listener = kosio::net::TcpListener::bind(addr_);
        if (!has_listener) {
            LOG_ERROR("{}", has_listener.error());
            co_return;
        }
        auto listener = std::move(has_listener.value());
        is_accepting_.store(true, std::memory_order_release);
        kosio::spawn(wait_for_ctrl_c());
        LOG_INFO("vrpc tcp server listening on {}", addr_);
        while (true) {
            auto has_stream = co_await listener.accept();
            if (!has_stream) {
                LOG_ERROR("{}", has_stream.error());
                continue;
            }
            if (is_shutdown_.load(std::memory_order_acquire)) {
                break;
            }
            auto& [stream, peer_addr] = has_stream.value();
            if (auto ret = stream.set_nodelay(true); !ret) {
                LOG_ERROR("{}", ret.error());
                continue;
            }
            auto conn = co_await manager_.assign(peer_addr, std::move(stream));
            if (!conn) {
                continue;
            }
            LOG_INFO("accept connection from {}", peer_addr);
            kosio::spawn(handle_request_loop(conn));
            kosio::spawn(send_response_loop(conn));
        }
        LOG_INFO("vrpc tcp server stop listening on {}", addr_);
        co_await listener.close();
        is_accepting_.store(false, std::memory_order_release);
    }

    static auto handle_request_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& sender = conn->sender_;
        auto& buf = conn->req_buf_;

        detail::RpcMessageHeader header{};
        while (true) {
            // 读取请求报文头
            if (auto ret = co_await stream.read_exact(
            {reinterpret_cast<char*>(&header), sizeof(header)}); !ret) {
                break;
            }

            // 校验消息大小
            auto msg_size = be32toh(header.msg_size);
            if (msg_size > detail::MAX_RPC_MESSAGE_SIZE) {
                LOG_ERROR("rpc response message too large");
                break;
            }

            // 读取请求报文
            if (auto ret = co_await stream.read_exact({buf.data(), msg_size}); !ret) {
                LOG_ERROR("{}", ret.error());
                break;
            }

            // 解析请求报文
            auto has_request = detail::RpcRequestMessage::parse_from(buf.data(), msg_size);
            if (!has_request) {
                LOG_ERROR("rpc request message parse failed");
                break;
            }
            auto request = std::move(has_request.value());

            if (auto ret = co_await sender.send(request); !ret) {
                LOG_ERROR("{}", ret.error());
                break;
            }
        }

        LOG_INFO("connection from {} closed", conn->addr_);
        sender.close();
    }

    auto send_response_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& receiver = conn->receiver_;
        auto cache = std::make_shared<detail::ServerCache>(conn->addr_);

        detail::RpcMessageHeader header;
        while (true) {
            auto has_request = co_await receiver.recv();
            if (!has_request) {
                break;
            }
            auto request = std::move(has_request.value());
            detail::set_server_cache(cache); // 更新缓存
            auto response = co_await call_method(request); // 进行 RPC 调用，获取回复报文 TODO: 启动协程？
            header.msg_size = htobe32(response.bytes_size());
            response.htobe(); // 转为网络字节序

            // 发送回复报文
            auto ret = co_await stream.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&header), sizeof(header)),
                std::span<const char>(reinterpret_cast<char*>(&response.seq_), sizeof(response.seq_)),
                std::span<const char>(reinterpret_cast<char*>(&response.status_code_), sizeof(response.status_code_)),
                std::span<const char>(reinterpret_cast<char*>(&response.err_msg_size_), sizeof(response.err_msg_size_)),
                response.err_msg_,
                std::span<const char>(reinterpret_cast<char*>(&response.payload_size_), sizeof(response.payload_size_)),
                response.payload_,
                std::span<const char>(reinterpret_cast<char*>(&response.check_sum_), sizeof(response.check_sum_))
            );

            if (!ret) {
                // LOG_ERROR("[{}] send rpc response message failed: {}", conn->addr_, ret.error());
            }
        }
        co_await stream.close();
        manager_.remove(conn->addr_.to_string());
    }

    [[REMEMBER_CO_AWAIT]]
    auto call_method(detail::RpcRequestMessage& request) -> kosio::async::Task<detail::RpcResponseMessage> {
        auto service_it = services_.find(request.service_name_);
        if (service_it == services_.end()) {
            co_return detail::RpcResponseMessage::make(
                request.seq_,
                vrpc::Status::kNotFound,
                std::format("rpc service {} not found", request.service_name_),
                "");
        }
        auto method_it = service_it->second.find(request.method_name_);
        if (method_it == service_it->second.end()) {
            co_return detail::RpcResponseMessage::make(
                request.seq_, vrpc::Status::kNotFound,
                std::format("rpc method {} not found",
                    request.method_name_),
                    "");
        }
        auto& method = method_it->second;
        co_return co_await method->run(request);
    }

    auto wait_for_ctrl_c() -> kosio::async::Task<void> {
        co_await kosio::signal::ctrl_c();
        co_await shutdown();
    }

private:
    std::atomic<bool>         is_accepting_{false};
    std::atomic<bool>         is_shutdown_{false};
    detail::RpcServiceMap     services_;
    detail::ConnectionManager manager_;
    detail::Config            config_;
    kosio::net::SocketAddr    addr_{};
};

class TcpServerBuilder : public detail::EndPointBuilder<TcpServer> {};
} // namespace vrpc