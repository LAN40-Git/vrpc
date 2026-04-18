#pragma once
#include "vrpc/core/detail/config.hpp"
#include "vrpc/core/detail/request.hpp"
#include <kosio/net.hpp>

namespace vrpc::detail {
class Connection {
public:
    explicit Connection(
        const kosio::net::SocketAddr& addr,
        kosio::net::TcpStream stream,
        RpcRequestSender sender,
        RpcRequestReceiver receiver)
        : addr_(addr)
        , stream_(std::move(stream))
        , sender_(std::move(sender))
        , receiver_(std::move(receiver))
        , req_buf_(MAX_RPC_MESSAGE_SIZE)
        , resp_buf_(MAX_RPC_MESSAGE_SIZE) {}

public:
    kosio::net::SocketAddr addr_;
    kosio::net::TcpStream  stream_;
    RpcRequestSender       sender_;
    RpcRequestReceiver     receiver_;
    std::vector<char>      req_buf_;
    std::vector<char>      resp_buf_;
    kosio::sync::Latch     latch_{2};
};
} // namespace vrpc::detail