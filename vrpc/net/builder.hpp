#pragma once
#include "vrpc/net/detail/config.hpp"
#include "vrpc/net/tcp/server.hpp"

namespace vrpc {
namespace detail {
    template <typename Server>
class ServerBuilder {
private:
    ServerBuilder() = default;

public:
    [[nodiscard]]
    auto set_ip(std::string_view ip) -> ServerBuilder& {
        config_.ip = ip;
        return *this;
    }

    [[nodiscard]]
    auto set_port(uint16_t port) -> ServerBuilder& {
        config_.port = port;
        return *this;
    }

    [[nodiscard]]
    auto set_max_connect_timeout(std::size_t max_connect_timeout) -> ServerBuilder& {
        config_.max_connect_timeout = max_connect_timeout;
        return *this;
    }

    [[nodiscard]]
    auto set_channel_capacity(std::size_t channel_capacity) -> ServerBuilder& {
        config_.channel_capacity = channel_capacity;
        return *this;
    }

    [[nodiscard]]
    auto set_thread_nums(std::size_t thread_nums) -> ServerBuilder& {
        config_.thread_nums = thread_nums;
        return *this;
    }

    // return an established end
    [[nodiscard]]
    auto build() -> Server {
        return Server{config_};
    }

public:
    [[nodiscard]]
    static auto options() -> ServerBuilder {
        return ServerBuilder{};
    }

    [[nodiscard]]
    static auto default_create() {
        return ServerBuilder{}.build();
    }

private:
    Config config_;
};
} // namespace detail

class TcpServerBuilder : public detail::ServerBuilder<TcpServer> {};

} // namespace vrpc