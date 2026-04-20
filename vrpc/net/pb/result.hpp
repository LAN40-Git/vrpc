#pragma once
#include <functional>
#include <kosio/sync.hpp>
#include "vrpc/common/concept.hpp"
#include "vrpc/net/pb/status.hpp"

namespace vrpc {
class RpcResult {
public:
    explicit RpcResult(unsigned char code, uint32_t payload_size)
        : code_(code), payload_size_(payload_size) {}

public:
    [[nodiscard]]
    auto code() const noexcept -> unsigned char { return code_; }

    [[nodiscard]]
    auto payload_size() const noexcept -> uint32_t { return payload_size_; }

public:
    static auto make(unsigned char code, uint32_t payload_size = 0) -> RpcResult {
        return RpcResult{code, payload_size};
    }

private:
    unsigned char code_;
    uint32_t      payload_size_;
};
using Method = std::function<kosio::async::Task<RpcResult>(std::string_view, std::span<char>)>;
using RpcCallback = std::function<kosio::async::Task<void>(Status::Code, std::string_view)>;

[[nodiscard]]
static auto make_result(Status::Code code, uint32_t payload_size = 0) -> RpcResult {
    return RpcResult::make(code, payload_size);
}

template <ProtobufMessage P>
[[nodiscard]]
static auto make_result(const P& response, std::span<char> resp_payload) -> RpcResult {
    const auto size = static_cast<uint32_t>(response.ByteSizeLong());
    if (size > resp_payload.size()) {
        return make_result(Status::kUnknown);
    }
    if (!response.SerializeToArray(resp_payload.data(), size)) {
        return make_result(Status::kUnknown);
    }
    return make_result(Status::kOk, size);
}
} // namespace vrpc
