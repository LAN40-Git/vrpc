#pragma once
#include <cstddef>
#include <thread>

namespace vrpc::detail {
static constexpr std::size_t SHUT_DOWN_WAITING_INTERVAL{10};

struct Config {
    // ip
    std::string ip{"127.0.0.1"};

    // port
    uint16_t port{19999};

    // 最大连接超时-ms
    std::size_t max_connect_timeout{60 * 1000};

    // 消息通道大小
    std::size_t channel_capacity{256};

    // 线程数
    std::size_t thread_nums{std::thread::hardware_concurrency()};
};
} // namespace vrpc::detail