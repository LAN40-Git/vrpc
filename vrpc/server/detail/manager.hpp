#pragma once
#include "vrpc/common/error.hpp"
#include "vrpc/core/detail/connection.hpp"

namespace vrpc::server {
class RpcServer;
} // namespace vrpc::server

namespace vrpc::server::detail {
struct ConnectionManager {
    friend class vrpc::server::RpcServer;
    using Connection = vrpc::detail::Connection;
public:
    [[REMEMBER_CO_AWAIT]]
    auto assign(const kosio::net::SocketAddr& addr, kosio::net::TcpStream stream) -> kosio::async::Task<Result<std::shared_ptr<Connection>>> {
        auto [sender, receiver] =
            vrpc::detail::RpcRequestChannel::make(vrpc::detail::RPC_REQUEST_CHANNEL_CAPACITY);
        auto new_conn = std::make_shared<Connection>(addr, std::move(stream), std::move(sender), std::move(receiver));
        auto addr_str = addr.to_string();
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        auto it = connections_.find(addr_str);
        if (it != connections_.end()) {
            co_return std::unexpected{make_error(Error::kRepeatedConnection)};
        }
        connections_.emplace(addr.to_string(), new_conn);
        co_return new_conn;
    }

    [[REMEMBER_CO_AWAIT]]
    auto remove(const std::string& addr) -> kosio::async::Task<void> {
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        connections_.erase(addr);
    }

private:
    using ConnectionMap = std::unordered_map<std::string, std::shared_ptr<Connection>>;
    kosio::sync::Mutex mutex_;
    ConnectionMap      connections_;
};
} // namespace vrpc::server::detail