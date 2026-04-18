#pragma once
#include <span>
#include <string>
#include <unordered_map>
#include "vrpc/core/type.hpp"
#include "vrpc/core/result.hpp"
#include "vrpc/core/detail/request.hpp"
#include "vrpc/server/detail/invoker.hpp"

namespace vrpc::server::detail {
class RpcInvoker {
public:
    void register_invoke(Type service_type, Type invoke_type, const RpcInvoke& invoke) {
        invokes_[service_type][invoke_type] = invoke;
    }

    [[REMEMBER_CO_AWAIT]]
    auto invoke(
        Type service_type,
        Type invoke_type,
        std::string_view req_payload,
        std::span<char> resp_payload) -> kosio::async::Task<RpcResult> {
        auto it_service = invokes_.find(service_type);
        if (it_service == invokes_.end()) {
            co_return make_result(StatusCode::kNotFound);
        }
        auto it_invoke = it_service->second.find(invoke_type);
        if (it_invoke == it_service->second.end()) {
            co_return make_result(StatusCode::kNotFound);
        }
        auto& invoke = it_invoke->second;
        co_return co_await invoke(req_payload, resp_payload);
    }

private:
    using InvokeMap = std::unordered_map<Type, std::unordered_map<Type, RpcInvoke>>;
    InvokeMap invokes_;
};
} // namespace vrpc::server::detail