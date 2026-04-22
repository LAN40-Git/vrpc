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
        kUnknown
    };

public:
    explicit Status(uint8_t code, std::string&& err_msg = "")
        : code_(code)
        , err_msg_(std::move(err_msg)) {}

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