#pragma once
#include <string_view>
#include <utility>

namespace vrpc {
class Status {
public:
    enum Code : uint8_t {
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
        kParseFailed,
        kSerializeFailed,
        kUnknown
    };

public:
    explicit Status(uint8_t code = kOk, std::string_view err_msg = "")
        : code_(code)
        , err_msg_(err_msg) {}

    explicit Status(Code code)
        : code_(static_cast<uint8_t>(code)) {}

public:
    [[nodiscard]]
    auto ok() const -> bool {
        return code_ == kOk;
    }

public:
    [[nodiscard]]
    auto code() const noexcept -> uint8_t {
        return code_;
    }

    [[nodiscard]]
    auto message() const noexcept -> std::string_view {
        return err_msg_;
    }

private:
    uint8_t     code_;
    std::string err_msg_;
};
} // namespace vrpc