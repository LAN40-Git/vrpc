#pragma once
#include "vrpc/net/detail/config.hpp"

namespace vrpc::detail {
template <typename EndPoint>
class EndPointBuilder {
private:
    EndPointBuilder() = default;

public:
    [[nodiscard]]
    auto set_ip(std::string_view ip) -> EndPointBuilder& {
        config_.ip = ip;
        return *this;
    }

    [[nodiscard]]
    auto set_port(uint16_t port) -> EndPointBuilder& {
        config_.port = port;
        return *this;
    }

    [[nodiscard]]
    auto set_thread_nums(std::size_t thread_nums) -> EndPointBuilder& {
        config_.thread_nums = thread_nums;
        return *this;
    }

    // return an established end
    [[nodiscard]]
    auto build() -> EndPoint {
        return EndPoint{config_};
    }

public:
    [[nodiscard]]
    static auto options() -> EndPointBuilder {
        return EndPointBuilder{};
    }

    [[nodiscard]]
    static auto default_create() {
        return EndPointBuilder{}.build();
    }

private:
    Config config_;
};
} // namespace vrpc::detail
