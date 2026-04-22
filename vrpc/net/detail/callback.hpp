#pragma once
#include <kosio/core.hpp>
#include <tbb/concurrent_hash_map.h>
#include "vrpc/net/pb/status.hpp"
#include "vrpc/net/pb/message.hpp"

namespace vrpc::detail {
class RpcCallbackBase {
public:
    virtual ~RpcCallbackBase() = default;
    virtual auto run(const RpcResponseMessage& message) -> kosio::async::Task<void> = 0;
};

template <typename Resp>
class RpcCallbackImpl final : public RpcCallbackBase {
public:
    using Callback = std::function<kosio::async::Task<void>(const Status&, const Resp&)>;
    explicit RpcCallbackImpl(Callback callback)
        : callback_(std::move(callback)) {}

public:
    [[REMEMBER_CO_AWAIT]]
    auto run(const RpcResponseMessage& message) -> kosio::async::Task<void>  override {
        Resp response;
        auto status = Status{message.status_code_};
        if (status.ok()) {
            if (!response.ParseFromString(message.payload_)) {
                status = Status{Status::kInternal, "parse rpc response message failed"};
            }
        }
        co_await callback_(status, response);
    }

private:
    Callback callback_;
};

using RpcCallback = std::unique_ptr<RpcCallbackBase>;
using RpcCallbackMap = tbb::concurrent_hash_map<uint64_t, RpcCallback>;
} // namespace vrpc::detail