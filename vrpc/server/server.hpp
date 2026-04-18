#pragma once
#include <ranges>
#include <kosio/net.hpp>
#include "vrpc/server/context.hpp"
#include "vrpc/server/detail/manager.hpp"
#include "vrpc/server/detail/invoker.hpp"
#include "vrpc/core/detail/protocol.hpp"

namespace vrpc::server {
namespace detail {
inline thread_local RpcContext tls_rpc_context;

static void set_rpc_context(const RpcContext& context) {
    tls_rpc_context = context;
}
} // namespace detail
[[nodiscard]]
static auto get_rpc_context() {
    return detail::tls_rpc_context;
}

class RpcServer {
public:
    explicit RpcServer(uint16_t port)
        : port_(port) {}

    // Delete copy
    RpcServer(const RpcServer&) = delete;
    auto operator=(const RpcServer&) -> RpcServer& = delete;

    // Delete move
    RpcServer(RpcServer&&) = delete;
    auto operator=(RpcServer&&) -> RpcServer& = delete;

public:
    void register_invoke(
        Type service_type,
        Type invoke_type,
        const RpcInvoke& invoke) {
        invoker_.register_invoke(service_type, invoke_type, invoke);
    }

public:
    [[REMEMBER_CO_AWAIT]]
    auto start() -> kosio::async::Task<void> {
        auto has_addr = kosio::net::SocketAddr::parse("0.0.0.0", port_);
        if (!has_addr) {
            LOG_ERROR("{}", has_addr.error());
            co_return;
        }
        auto addr = has_addr.value();
        auto has_listener = kosio::net::TcpListener::bind(addr);
        if (!has_listener) {
            LOG_ERROR("{}", has_listener.error());
            co_return;
        }
        auto listener = std::move(has_listener.value());

        is_running_.store(true, std::memory_order_release);
        LOG_INFO("vrpc server start on {}", addr_);
        while (is_running_.load(std::memory_order_acquire)) {
            auto has_stream = co_await listener.accept();
            if (!has_stream) {
                continue;
            }
            auto& [stream, peer_addr] = has_stream.value();
            if (auto ret = stream.set_nodelay(true); !ret) {
                LOG_ERROR("{}", ret.error());
                continue;
            }
            auto has_conn = co_await manager_.assign(peer_addr, std::move(stream));
            if (!has_conn) {
                LOG_ERROR("{}", has_conn.error());
                continue;
            }
            LOG_INFO("accept connection from {}", peer_addr);
            auto conn = std::move(has_conn.value());
            kosio::spawn(handle_request_loop(conn));
            kosio::spawn(send_response_loop(conn));
        }
        co_await listener.close();
        latch_.count_down();
    }

    [[REMEMBER_CO_AWAIT]]
    auto stop() -> kosio::async::Task<void> {
        auto has_addr = kosio::net::SocketAddr::parse("0.0.0.0", port_);
        if (!has_addr) {
            LOG_ERROR("{}", has_addr.error());
            co_return;
        }
        auto addr = has_addr.value();

        is_running_.store(false, std::memory_order_release);
        co_await kosio::net::TcpStream::connect(addr);
        co_await latch_.wait();

        auto& connections = manager_.connections_;
        for (auto& connection : connections | std::views::values) {
            connection->sender_.close();
            co_await connection->latch_.wait();
        }
        LOG_INFO("vrpc server on {} stop", addr);
    }

private:
    static auto handle_request_loop(std::shared_ptr<vrpc::detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& sender = conn->sender_;
        auto& buf = conn->req_buf_;

        vrpc::detail::RpcRequestHeader req_header;
        while (true) {
            if (auto ret = co_await stream.read_exact(
            {reinterpret_cast<char*>(&req_header), sizeof(req_header)}); !ret) {
                break;
            }

            auto request_id = be64toh(req_header.request_id);
            auto payload_size = be32toh(req_header.payload_size);
            auto service_type = req_header.service_type;
            auto invoke_type = req_header.invoke_type;

            if (payload_size > buf.size()) {
                LOG_ERROR("receive unusual rpc message from {}", conn->addr_);
                break;
            }

            if (auto ret = co_await stream.read_exact({buf.data(), payload_size}); !ret) {
                LOG_ERROR("{}", ret.error());
                break;
            }

            vrpc::detail::RpcRequest request;
            request.request_id = request_id;
            request.service_type = service_type;
            request.invoke_type = invoke_type;
            request.req_payload = std::string{buf.data(), payload_size};
            if (auto ret = co_await sender.send(request); !ret) {
                LOG_ERROR("{}", ret.error());
                break;
            }
        }

        sender.close();
        co_await conn->latch_.arrive_and_wait();
    }

    auto send_response_loop(std::shared_ptr<vrpc::detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& receiver = conn->receiver_;
        auto& buf = conn->resp_buf_;
        auto rpc_context = RpcContext{conn->addr_};

        vrpc::detail::RpcResponseHeader resp_header;
        while (true) {
            auto has_request = co_await receiver.recv();
            if (!has_request) {
                // 只有在通道关闭时才会退出
                break;
            }
            auto request = std::move(has_request.value());
            // 设置上下文，服务端可以通过 get_rpc_context 函数获取上下文
            // invoke 中可以获取当前客户端的信息
            detail::set_rpc_context(rpc_context);

            // 进行 RPC 调用
            auto result = co_await invoker_.invoke(
                request.service_type,
                request.invoke_type,
                request.req_payload,
                {buf.data(), buf.capacity()});
            resp_header.request_id = htobe64(request.request_id);
            resp_header.payload_size = htobe32(result.payload_size());
            resp_header.code = result.code();

            auto ret = co_await stream.write_vectored(
                std::span<const char>(reinterpret_cast<char*>(&resp_header), sizeof(resp_header)),
                std::span<const char>(buf.data(), result.payload_size())
            );

            if (!ret) {
                LOG_ERROR("failed to send response to {}: {}", conn->addr_, ret.error());
            }
        }
        receiver.close();
        co_await conn->latch_.arrive_and_wait();
        co_await manager_.remove(conn->addr_.to_string());
    }

private:
    uint16_t                  port_;
    std::atomic<bool>         is_running_{false};
    detail::RpcInvoker        invoker_;
    detail::ConnectionManager manager_;
    kosio::sync::Latch        latch_{1};
};
} // namespace vrpc::server