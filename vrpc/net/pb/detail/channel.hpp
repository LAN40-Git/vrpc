#pragma once
#include <kosio/sync.hpp>
#include "vrpc/net/pb/message.hpp"

namespace vrpc::detail {
using RpcRequestChannel = kosio::sync::Channel<RpcRequestMessage>;
using RpcRequestSender = RpcRequestChannel::Sender;
using RpcRequestReceiver = RpcRequestChannel::Receiver;
using RpcRequestSenderPtr = std::shared_ptr<RpcRequestChannel::Sender>;
using RpcRequestReceiverPtr = std::shared_ptr<RpcRequestChannel::Receiver>;
} // namespace vrpc::detail