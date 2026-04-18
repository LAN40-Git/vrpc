#pragma once
#include <cstddef>

namespace vrpc::detail {
// 连接到 RPC 服务端的超时
constexpr std::size_t CONNECT_TIMEOUT_MS = 200;
// 关闭连接时，循环等待状态变化的时间
constexpr std::size_t WAITING_INTERVAL_MS = 50;
// RPC 支持的最大消息，单位是字节
constexpr std::size_t MAX_RPC_MESSAGE_SIZE = 4 * 1024 * 1024;
// RPC 通道大小
constexpr std::size_t RPC_REQUEST_CHANNEL_CAPACITY = 256;
} // namespace vrpc::detail