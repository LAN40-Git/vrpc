#pragma once
#include <ranges>
#include <tbb/concurrent_hash_map.h>
#include <kosio/net.hpp>
#include "vrpc/core/detail/config.hpp"
#include "vrpc/core/detail/request.hpp"

namespace vrpc {
class TcpServer;
} // namespace vrpc

namespace vrpc::detail {
class Connection {
public:
    explicit Connection(
        const kosio::net::SocketAddr& addr,
        kosio::net::TcpStream stream,
        RequestSender sender,
        RequestReceiver receiver)
        : addr_(addr)
        , stream_(std::move(stream))
        , sender_(std::move(sender))
        , receiver_(std::move(receiver))
        , req_buf_(MAX_MESSAGE_SIZE)
        , resp_buf_(MAX_MESSAGE_SIZE) {}

public:
    kosio::net::SocketAddr addr_;
    kosio::net::TcpStream  stream_;
    RequestSender          sender_;
    RequestReceiver        receiver_;
    std::vector<char>      req_buf_;
    std::vector<char>      resp_buf_;
};

class ConnectionManager {
    friend class vrpc::TcpServer;
    using ConnectionMap = tbb::concurrent_hash_map<std::string, std::shared_ptr<Connection>>;
public:
    [[REMEMBER_CO_AWAIT]]
    auto assign(const kosio::net::SocketAddr& addr, kosio::net::TcpStream stream) -> kosio::async::Task<std::shared_ptr<Connection>> {
        auto [sender, receiver] =
            RequestChannel::make(REQUEST_CHANNEL_CAPACITY);
        auto new_conn = std::make_shared<Connection>(addr, std::move(stream), std::move(sender), std::move(receiver));
        auto addr_str = addr.to_string();
        {
            ConnectionMap::accessor acc;
            if (connections_.find(acc, addr_str)) {
                co_return nullptr;
            }
        }
        connections_.emplace(addr.to_string(), new_conn);
        co_return new_conn;
    }

    void remove(const std::string& addr) {
        connections_.erase(addr);
    }

    auto empty() const -> bool {
        return connections_.empty();
    }

    [[REMEMBER_CO_AWAIT]]
    auto cancel_all() -> kosio::async::Task<void> {
        for (const auto& connection : connections_ | std::views::values) {
            co_await kosio::io::cancel(connection->stream_.fd(), IORING_ASYNC_CANCEL_ALL);
        }
    }

private:
    ConnectionMap connections_;
};
} // namespace vrpc::server::detail