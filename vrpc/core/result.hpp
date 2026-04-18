#pragma once
#include <functional>
#include <kosio/sync.hpp>
#include "vrpc/core/status.hpp"

namespace vrpc {
class RpcResult {
public:
    explicit RpcResult(StatusCode code, uint32_t payload_size)
        : code_(code), payload_size_(payload_size) {}

public:
    [[nodiscard]]
    auto code() const noexcept -> StatusCode { return code_; }

    [[nodiscard]]
    auto payload_size() const noexcept -> uint32_t { return payload_size_; }

public:
    static auto make(StatusCode code, uint32_t payload_size = 0) -> RpcResult {
        return RpcResult{code, payload_size};
    }

private:
    StatusCode code_;
    uint32_t   payload_size_;
};
using RpcInvoke = std::function<kosio::async::Task<RpcResult>(std::string_view, std::span<char>)>;
using RpcCallback = std::function<kosio::async::Task<void>(StatusCode, std::string_view)>;

[[nodiscard]]
static auto make_result(StatusCode code, uint32_t payload_size = 0) -> RpcResult {
    return RpcResult::make(code, payload_size);
}

template <typename Response>
[[nodiscard]]
static auto make_result(const Response& response, std::span<char> resp_payload) -> RpcResult {
    const auto size = static_cast<uint32_t>(response.ByteSizeLong());
    if (size > resp_payload.size()) {
        return make_result(StatusCode::kInternal);
    }
    if (!response.SerializeToArray(resp_payload.data(), size)) {
        return make_result(StatusCode::kInternal);
    }
    return make_result(StatusCode::kOk, size);
}
} // namespace vrpc
