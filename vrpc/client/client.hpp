#pragma once
#include <kosio/net.hpp>
#include <kosio/sync.hpp>
#include "vrpc/core/detail/connection.hpp"

namespace vrpc::client {
class RpcClient {
    enum class Status {
        kConnected = 0,
        kConnecting,
        kStopping,
        kStopped
    };

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
        const RpcCallback& callback) -> kosio::async::Task<Result<void>> {
        static std::atomic<uint64_t> current_request_id{0};

        auto sender = std::atomic_load(&sender_);
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

        auto [sender, receiver] =
            detail::RpcRequestChannel::make(detail::RPC_REQUEST_CHANNEL_CAPACITY);
        auto conn = std::make_shared<detail::Connection>(addr, std::move(stream), std::move(sender), std::move(receiver));
        kosio::spawn(send_request_loop(conn));
        kosio::spawn(handle_response_loop(conn));
    }

private:
    auto send_request_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {

    }

    auto handle_response_loop(std::shared_ptr<detail::Connection> conn) -> kosio::async::Task<void> {

    }

private:
    std::string              server_ip_;
    uint16_t                 server_port_;
    detail::RpcRequestSender sender_{nullptr};
};
} // namespace vrpc::client