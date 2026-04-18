#pragma once
#include <kosio/net.hpp>

namespace vrpc::server {
// RPC 客户端上下文
struct RpcContext {
    // 客户端地址
    kosio::net::SocketAddr addr;
};
} // namespace vrpc::server