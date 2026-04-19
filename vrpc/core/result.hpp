#pragma once
#include <functional>
#include <kosio/sync.hpp>
#include "vrpc/core/status.hpp"

namespace vrpc {
class InvokeResult {
public:
    explicit InvokeResult(StatusCode code, uint32_t payload_size)
        : code_(code), payload_size_(payload_size) {}

public:
    [[nodiscard]]
    auto code() const noexcept -> StatusCode { return code_; }

    [[nodiscard]]
    auto payload_size() const noexcept -> uint32_t { return payload_size_; }

public:
    static auto make(StatusCode code, uint32_t payload_size = 0) -> InvokeResult {
        return InvokeResult{code, payload_size};
    }

private:
    StatusCode code_;
    uint32_t   payload_size_;
};
using Invoke = std::function<kosio::async::Task<InvokeResult>(std::string_view, std::span<char>)>;
using Callback = std::function<kosio::async::Task<void>(StatusCode, std::string_view)>;

[[nodiscard]]
static auto make_result(StatusCode code, uint32_t payload_size = 0) -> InvokeResult {
    return InvokeResult::make(code, payload_size);
}

template <typename T>
requires requires(const T& msg, void* data, int size) {
    { msg.ByteSizeLong() } -> std::same_as<size_t>;
    { msg.SerializeToArray(data, size) } -> std::same_as<bool>;
}
[[nodiscard]]
static auto make_result(const T& response, std::span<char> resp_payload) -> InvokeResult {
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
