#pragma once
#include <string>
#include <kosio/sync.hpp>
#include "vrpc/core/result.hpp"
#include "vrpc/core/type.hpp"

namespace vrpc::detail {
struct RpcRequest {
    uint64_t    request_id{};
    Type        service_type{};
    Type        invoke_type{};
    std::string req_payload{};
    RpcCallback callback{};
};

using RpcRequestChannel = kosio::sync::Channel<RpcRequest>;
using RpcRequestSender = RpcRequestChannel::Sender;
using RpcRequestReceiver = RpcRequestChannel::Receiver;
} // namespace vrpc::detail