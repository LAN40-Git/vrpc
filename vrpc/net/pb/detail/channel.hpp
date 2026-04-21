#pragma once
#include <kosio/sync.hpp>
#include "vrpc/net/pb/status.hpp"
#include "vrpc/net/pb/detail/message.hpp"

namespace vrpc::detail {
using RpcRequestChannel = kosio::sync::Channel<RpcRequestMessage>;
using RpcRequestSender = RpcRequestChannel::Sender;
using RpcRequestReceiver = RpcRequestChannel::Receiver;

using RpcResponseChannel = kosio::sync::Channel<RpcResponseMessage>;
using RpcResponseSender = RpcResponseChannel::Sender;
using RpcResponseReceiver = RpcResponseChannel::Receiver;
} // namespace vrpc::detail