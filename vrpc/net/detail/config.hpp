#pragma once
#include <cstddef>
#include <thread>

namespace vrpc::detail {
struct Config {
    // 最大消息长度-bytes
    const std::size_t max_message_size{4 * 1024 * 1024};

    // ip
    std::string ip{"127.0.0.1"};

    // port
    uint16_t port{19999};

    // 关闭连接时循环中等待的时间间隔
    std::size_t shutdown_waiting_interval{10};

    // 最大连接超时-ms
    std::size_t max_connect_timeout{60 * 1000};

    // 消息通道大小
    std::size_t channel_capacity{256};

    // 线程数
    std::size_t thread_nums{std::thread::hardware_concurrency()};
};
} // namespace vrpc::detail