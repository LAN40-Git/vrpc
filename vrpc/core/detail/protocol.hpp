#pragma once
#include "vrpc/core/type.hpp"
#include "vrpc/core/status.hpp"

namespace vrpc::detail {
struct RpcRequestHeader {
    uint64_t request_id{0};
    Type     service_type{0};
    Type     invoke_type{0};
    uint32_t payload_size{0};
};

struct RpcResponseHeader {
    uint64_t   request_id{0};
    uint32_t   payload_size{0};
    StatusCode code{0};
};
} // namespace vrpc::detail