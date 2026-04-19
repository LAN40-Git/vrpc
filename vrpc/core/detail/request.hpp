#pragma once
#include <string>
#include <kosio/sync.hpp>
#include "vrpc/core/result.hpp"
#include "vrpc/core/type.hpp"

namespace vrpc::detail {
struct Request {
    uint64_t    request_id{};
    Type        service_type{};
    Type        invoke_type{};
    std::string req_payload{};
    Callback    callback{};
};
using RequestChannel = kosio::sync::Channel<Request>;
using RequestSender = RequestChannel::Sender;
using RequestReceiver = RequestChannel::Receiver;
} // namespace vrpc::detail