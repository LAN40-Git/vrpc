#pragma once

namespace vrpc {
enum class StatusCode : unsigned char {
    kUnknown = 0,
    kOk,
    kCancelled,
    kNotFound,
    kInvalidArgument,
    kDeadlineExceeded,
    kPermissionDenied,
    kUnavailable,
    kInternal
};
} // namespace vrpc