#pragma once
#include <ranges>
#include <kosio/net.hpp>
#include "vrpc/common/concept.hpp"
#include "vrpc/server/detail/context.hpp"
#include "vrpc/server/detail/manager.hpp"
#include "vrpc/core/detail/protocol.hpp"

namespace vrpc {
class TcpServer {
public:
    explicit TcpServer(uint16_t port, std::string_view ip = "0.0.0.0")
        : port_(port)
        , ip_(ip) {}

    // Delete copy
    TcpServer(const TcpServer&) = delete;
    auto operator=(const TcpServer&) -> TcpServer& = delete;

    // Delete move
    TcpServer(TcpServer&&) = delete;
    auto operator=(TcpServer&&) -> TcpServer& = delete;

public:
    [[nodiscard]]
    auto port() const noexcept -> uint16_t {
        return port_;
    }

    [[nodiscard]]
    auto ip() const noexcept -> std::string_view {
        return ip_;
    }

public:
    template <VrpcType S, VrpcType I>
    void register_invoke(
        S service_type,
        I invoke_type,
        const Method& invoke) {
        invokes_[static_cast<Type>(service_type)][static_cast<Type>(invoke_type)] = invoke;
    }

public:
    [[REMEMBER_CO_AWAIT]]
    auto wait() -> kosio::async::Task<void> {
        auto has_addr = kosio::net::SocketAddr::parse(ip_, port_);
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

        is_shutdown_.store(false, std::memory_order_release);
        LOG_INFO("vrpc server listening on {}", addr);
        while (!is_shutdown_.load(std::memory_order_acquire)) {
            auto has_stream = co_await listener.accept();
            if (!has_stream) {
                continue;
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
        co_await listener.close();
        co_await latch_.arrive_and_wait();
    }

    [[REMEMBER_CO_AWAIT]]
    auto shutdown() -> kosio::async::Task<void> {
        auto has_addr = kosio::net::SocketAddr::parse("0.0.0.0", port_);
        if (!has_addr) {
            LOG_ERROR("{}", has_addr.error());
            co_return;
        }
        auto addr = has_addr.value();

        is_shutdown_.store(true, std::memory_order_release);
        co_await kosio::net::TcpStream::connect(addr);

        while (!manager_.empty()) {
            co_await manager_.cancel_all();
            co_await kosio::time::sleep(detail::WAITING_INTERVAL_MS);
        }
        LOG_INFO("vrpc server on {} stop", addr);
        co_await latch_.arrive_and_wait();
    }

private:
    static auto handle_request_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {
        auto& stream = conn->stream_;
        auto& sender = conn->sender_;
        auto& buf = conn->req_buf_;

        detail::RequestHeader req_header;
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
                LOG_ERROR("message from {} too large", conn->addr_);
                break;
            }

            if (auto ret = co_await stream.read_exact({buf.data(), payload_size}); !ret) {
                LOG_ERROR("{}", ret.error());
                break;
            }

            detail::Request request;
            request.request_id = request_id;
            request.service_type = service_type;
            request.invoke_type = invoke_type;
            request.req_payload = std::string{buf.data(), payload_size};
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
        auto& buf = conn->resp_buf_;
        auto context = std::make_shared<detail::ServerConext>(conn->addr_);

        detail::ResponseHeader resp_header;
        while (true) {
            auto has_request = co_await receiver.recv();
            if (!has_request) {
                break;
            }
            auto request = std::move(has_request.value());
            detail::set_context(context);

            // 进行 RPC 调用
            auto result = co_await invoke(
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
        manager_.remove(conn->addr_.to_string());
    }


    [[REMEMBER_CO_AWAIT]]
    auto invoke(
        Type service_type,
        Type invoke_type,
        std::string_view req_payload,
        std::span<char> resp_payload) -> kosio::async::Task<RpcResult> {
        auto it_service = invokes_.find(service_type);
        if (it_service == invokes_.end()) {
            co_return make_result(StatusCode::kNotFound);
        }
        auto it_invoke = it_service->second.find(invoke_type);
        if (it_invoke == it_service->second.end()) {
            co_return make_result(StatusCode::kNotFound);
        }
        auto& invoke = it_invoke->second;
        co_return co_await invoke(req_payload, resp_payload);
    }

private:
    using InvokeMap = std::unordered_map<Type, std::unordered_map<Type, Method>>;

    kosio::sync::Latch        latch_{2};
    uint16_t                  port_;
    std::string               ip_;
    std::atomic<bool>         is_shutdown_{true};
    InvokeMap                 invokes_;
    detail::ConnectionManager manager_;
};
} // namespace vrpc