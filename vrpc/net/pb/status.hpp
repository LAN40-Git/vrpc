#pragma once
#include <string_view>

namespace vrpc {
class Status {
public:
    enum Code : unsigned char {
        kOk = 0,
        kAborted,
        kAlreadyExists,
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

public:
    explicit Status(unsigned char code = kOk)
        : code_(code) {}

    explicit Status(Code code)
        : code_(static_cast<unsigned char>(code)) {}

public:
    [[nodiscard]]
    auto ok() const -> bool {
        return code_ == kOk;
    }

    [[nodiscard]]
    auto is_cancelled() const -> bool {
        return code_ == kCancelled;
    }

public:
    [[nodiscard]]
    auto code() const noexcept -> unsigned char {
        return code_;
    }

    [[nodiscard]]
    auto message() const noexcept -> std::string_view {
        switch (code_) {
        case kOk:
            return "ok";
        case kAborted:
            return "aborted";
        case kAlreadyExists:
            return "already exists";
        case kCancelled:
            return "cancelled";
        case kNotFound:
            return "not found";
        case kInvalidArgument:
            return "invalid argument";
        case kDeadlineExceeded:
            return "deadline exceeded";
        case kPermissionDenied:
            return "permission denied";
        case kUnavailable:
            return "unavailable";
        case kInternal:
            return "internal error";
        case kResourceExhausted:
            return "resource exhausted";
        case kUnknown:
            return "unknown error";
        default:
            return "invalid status code";
        }
    }

private:
    unsigned char code_;
};
} // namespace vrpc