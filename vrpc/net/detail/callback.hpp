#pragma once
#include <kosio/core.hpp>
#include "vrpc/net/pb/status.hpp"

namespace vrpc::detail {
class RpcCallbackBase {
public:
    virtual ~RpcCallbackBase() = default;
    virtual auto run(Status status, const std::string& payload) -> kosio::async::Task<void> = 0;
};

template <typename Resp>
class RpcCallbackImpl final : public RpcCallbackBase {
public:
    using Callback = std::function<kosio::async::Task<void>(Status, const Resp&)>;
    explicit RpcCallbackImpl(Callback callback)
        : callback_(std::move(callback)) {}

public:
    [[REMEMBER_CO_AWAIT]]
    auto run(Status status, const std::string& payload) -> kosio::async::Task<void>  override {
        Resp resp;
        if (status.ok() && !resp.ParseFromString(payload)) {
            status = Status{Status::kParseFailed, "parse rpc response failed"};
        }
        co_await callback_(status, resp);
    }

private:
    Callback callback_;
};
} // namespace vrpc::detail