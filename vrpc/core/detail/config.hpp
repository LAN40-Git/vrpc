#pragma once
#include <cstddef>

namespace vrpc::detail {
// 连接重试初始延迟
constexpr std::size_t INITIAL_CONNECT_BLOCK_OFF = 1000;
// 连接重试最大延迟
constexpr std::size_t MAX_CONNECT_BLOCK_OFF = 10 * 1000;
// 连接重试增长系数
constexpr std::size_t CONNECT_MULTIPLIER = 2;
// 关闭连接时，循环等待状态变化的时间
constexpr std::size_t WAITING_INTERVAL_MS = 50;
// RPC 支持的最大消息，单位是字节
constexpr std::size_t MAX_MESSAGE_SIZE = 4 * 1024 * 1024;
// RPC 通道大小
constexpr std::size_t REQUEST_CHANNEL_CAPACITY = 256;
} // namespace vrpc::detail