#pragma once
#include <kosio/sync.hpp>

namespace vrpc {
class RpcMessage;
} // namespace vrpc

namespace vrpc::detail {
using RpcChannel = kosio::sync::Channel<RpcMessage>;
using RpcSender = RpcChannel::Sender;
using RpcReceiver = RpcChannel::Receiver;
} // namespace vrpc::detail