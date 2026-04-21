#pragma once
#include <kosio/net.hpp>

namespace vrpc::detail {
struct ServerCache {
    // 客户端地址
    kosio::net::SocketAddr peer_addr;
};
inline thread_local std::shared_ptr<ServerCache> tls_server_cache{nullptr};

static void set_server_cache(const std::shared_ptr<ServerCache>& context) {
    tls_server_cache = context;
}
} // namespace vrpc::detail

namespace vrpc {
// 此函数只能在注册过的 RPC 调用中使用，否则断言可能会失败
[[nodiscard]]
static auto get_server_cache() -> std::shared_ptr<detail::ServerCache> {
    assert(detail::tls_server_cache);
    return detail::tls_server_cache;
}
} // namespace vrpc