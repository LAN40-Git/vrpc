#pragma once
#include <cstddef>
#include <thread>

namespace vrpc::detail {
static constexpr std::size_t SHUT_DOWN_WAITING_INTERVAL{30};
static constexpr std::size_t CHANNEL_CAPACITY = 256;
static constexpr double MULTIPLIER = 1.6;
static constexpr std::size_t BASE_DELAY = 1000; // 1s

struct Config {
    // [服务端/客户端]的[运行/连接] IP
    std::string ip{"127.0.0.1"};

    // [服务端/客户端]的[运行/连接]端口
    uint16_t port{8080};

    // 最小连接超时
    std::size_t min_connect_timeout{20*1000};

    // 最大阻塞时间（连接超时）
    std::size_t max_backoff{120*1000};

    // 运行时线程数
    std::size_t thread_nums{std::thread::hardware_concurrency()};
};
} // namespace vrpc::detail