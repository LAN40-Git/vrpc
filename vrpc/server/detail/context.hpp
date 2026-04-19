#pragma once
#include <kosio/net.hpp>

namespace vrpc::detail {
// 服务端上下文，其中缓存了当前客户端的信息，可以通过
// vrpc::server::get_context 获取此上下文
struct ServerConext {
    // 客户端地址
    kosio::net::SocketAddr addr;
};
inline thread_local std::shared_ptr<ServerConext> tls_server_context{nullptr};

static void set_context(const std::shared_ptr<ServerConext>& context) {
    tls_server_context = context;
}
} // namespace vrpc::detail

namespace vrpc {
// 此函数只能在注册过的 RPC 调用中使用，否则断言可能会失败
[[nodiscard]]
static auto get_context() -> std::shared_ptr<detail::ServerConext> {
    assert(detail::tls_server_context);
    return detail::tls_server_context;
}
} // namespace vrpc