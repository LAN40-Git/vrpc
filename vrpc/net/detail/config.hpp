#pragma once
#include <cstddef>
#include <thread>

namespace vrpc::detail {
static constexpr std::size_t SHUT_DOWN_WAITING_INTERVAL{10};
static constexpr std::size_t CHANNEL_CAPACITY = 256;
static constexpr double MIN_CONNECT_TIMEOUT = 300;

struct Config {
    // [服务端/客户端]的[运行/连接] IP
    std::string ip{"127.0.0.1"};

    // [服务端/客户端]的[运行/连接]端口
    uint16_t port{8080};

    // 最大连接超时
    std::size_t max_connect_timeout{6000}; // ms

    // 最大重连次数
    std::size_t max_connect_retry_times{5};

    // 运行时线程数
    std::size_t thread_nums{std::thread::hardware_concurrency()};
};
} // namespace vrpc::detail