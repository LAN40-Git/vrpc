#pragma once
#include <kosio/core.hpp>
#include "vrpc/net/pb/status.hpp"
#include "vrpc/net/pb/message.hpp"

namespace vrpc::detail {
class RpcMethodBase {
public:
    virtual ~RpcMethodBase() = default;
    virtual auto run(RpcRequestMessage& message) -> kosio::async::Task<RpcResponseMessage> = 0;
};

template <typename Req, typename Resp>
class RpcMethodImpl final : public RpcMethodBase {
public:
    using Method = std::function<kosio::async::Task<Resp>(const Req&)>;
    explicit RpcMethodImpl(Method method)
        : method_(std::move(method)) {}

public:
    [[REMEMBER_CO_AWAIT]]
    auto run(RpcRequestMessage& message) -> kosio::async::Task<RpcResponseMessage> override {
        Req request;
        if (!request.ParseFromString(message.payload_)) {
            co_return RpcResponseMessage::make(message.seq_, Status::kInternal, "parse rpc request message failed");
        }
        auto response = co_await method_(request);
        co_return RpcResponseMessage::make(message.seq_, Status::kOk, "", response.SerializeAsString());
    }

private:
    Method method_;
};

using RpcMethod = std::unique_ptr<RpcMethodBase>;
using RpcMethodMap = std::unordered_map<std::string, RpcMethod>;
using RpcServiceMap = std::unordered_map<std::string, RpcMethodMap>;
} // namespace vrpc::detail