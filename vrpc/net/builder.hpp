#pragma once
#include "vrpc/net/detail/config.hpp"

namespace vrpc {
class ConfigBuilder {
private:
    ConfigBuilder() = default;

public:
    [[nodiscard]]
    auto set_ip(std::string_view ip) -> ConfigBuilder& {
        config_.ip = ip;
        return *this;
    }

    [[nodiscard]]
    auto set_port(uint16_t port) -> ConfigBuilder& {
        config_.port = port;
        return *this;
    }

    [[nodiscard]]
    auto set_shutdown_waiting_interval(std::size_t shutdown_waiting_interval) -> ConfigBuilder& {
        config_.shutdown_waiting_interval = shutdown_waiting_interval;
        return *this;
    }

    [[nodiscard]]
    auto set_max_connect_timeout(std::size_t max_connect_timeout) -> ConfigBuilder& {
        config_.max_connect_timeout = max_connect_timeout;
        return *this;
    }

    [[nodiscard]]
    auto set_channel_capacity(std::size_t channel_capacity) -> ConfigBuilder& {
        config_.channel_capacity = channel_capacity;
        return *this;
    }

    [[nodiscard]]
    auto set_thread_nums(std::size_t thread_nums) -> ConfigBuilder& {
        config_.thread_nums = thread_nums;
        return *this;
    }

    // return an established end
    [[nodiscard]]
    auto build() -> detail::Config {
        return config_;
    }

public:
    [[nodiscard]]
    static auto options() -> ConfigBuilder {
        return ConfigBuilder{};
    }

    [[nodiscard]]
    static auto default_create() {
        return ConfigBuilder{}.build();
    }

private:
    detail::Config config_;
};
} // namespace vrpc