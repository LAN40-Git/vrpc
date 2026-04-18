#pragma once
#include "vrpc/common/error.hpp"
#include "vrpc/core/detail/config.hpp"
#include "vrpc/core/detail/request.hpp"
#include <kosio/net.hpp>

namespace vrpc::server {
class RpcServer;
} // namespace vrpc::server

namespace vrpc::server::detail {
class Connection {
public:
    explicit Connection(
        const kosio::net::SocketAddr& addr,
        kosio::net::TcpStream stream,
        vrpc::detail::RpcRequestSender sender,
        vrpc::detail::RpcRequestReceiver receiver)
        : addr_(addr)
        , stream_(std::move(stream))
        , sender_(std::move(sender))
        , receiver_(std::move(receiver))
        , req_buf_(vrpc::detail::MAX_RPC_MESSAGE_SIZE)
        , resp_buf_(vrpc::detail::MAX_RPC_MESSAGE_SIZE) {}

public:
    kosio::net::SocketAddr           addr_;
    kosio::net::TcpStream            stream_;
    vrpc::detail::RpcRequestSender   sender_;
    vrpc::detail::RpcRequestReceiver receiver_;
    std::vector<char>                req_buf_;
    std::vector<char>                resp_buf_;
};

class ConnectionManager {
    friend class vrpc::server::RpcServer;
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

    [[REMEMBER_CO_AWAIT]]
    auto empty() -> kosio::async::Task<bool> {
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        co_return connections_.empty();
    }

    [[REMEMBER_CO_AWAIT]]
    auto cancel_all() -> kosio::async::Task<void> {
        co_await mutex_.lock();
        std::lock_guard lock{mutex_, std::adopt_lock};
        for (auto& connection : connections_ | std::views::values) {
            co_await kosio::io::cancel(connection->stream_.fd(), IORING_ASYNC_CANCEL_ALL);
        }
    }

private:
    using ConnectionMap = std::unordered_map<std::string, std::shared_ptr<Connection>>;
    kosio::sync::Mutex mutex_;
    ConnectionMap      connections_;
};
} // namespace vrpc::server::detail