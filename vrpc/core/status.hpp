#pragma once

namespace vrpc {
enum class StatusCode : unsigned char {
    kOk = 0,
    kCancelled,
    kNotFound,
    kInvalidArgument,
    kDeadlineExceeded,
    kPermissionDenied,
    kUnavailable,
    kInternal,
    kResourceExhausted,
    kUnknown
};
} // namespace vrpc