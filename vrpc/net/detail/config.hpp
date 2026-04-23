#pragma once
#include <cstddef>
#include <thread>

namespace vrpc::detail {
static constexpr std::size_t SHUT_DOWN_WAITING_INTERVAL{30};
static constexpr std::size_t CHANNEL_CAPACITY = 512;
static constexpr double MULTIPLIER = 1.6;
static constexpr std::size_t BASE_DELAY = 1000; // 1s
static constexpr std::size_t MIN_CONNECT_TIMEOUT = 20 * 1000; // 20s
static constexpr std::size_t MAX_BACKOFF = 120 * 1000; // 120s

struct Config {
    // 服务端的监听 IP
    std::string ip{"127.0.0.1"};

    // 服务端的监听端口
    uint16_t port{8080};

    // 运行时线程数
    std::size_t thread_nums{std::thread::hardware_concurrency()};
};
} // namespace vrpc::detail