#pragma once
#include <cassert>
#include <expected>
#include <string_view>
#include <format>

namespace vrpc {
class Error {
public:
    enum ErrorCode {
        kConnClosed = 0,
    };

public:
    explicit Error(int err_code)
        : error_code_{err_code} {}

public:
    [[nodiscard]]
    auto value() const noexcept -> int {
        return error_code_;
    }

    [[nodiscard]]
    auto message() const noexcept -> std::string_view {
        switch (error_code_) {
            case kConnClosed:
                return "connection has closed";
            default:
                return "unknown error";
        }
    }

private:
    int error_code_;
};

[[nodiscard]]
static inline auto make_error(int error_code) -> Error {
    assert(error_code >= 0);
    return Error{error_code};
}

template <typename T>
using Result = std::expected<T, Error>;
} // namespace vrpc

namespace std {
template <>
struct formatter<vrpc::Error> {
public:
    constexpr auto parse(format_parse_context &context) {
        auto it{context.begin()};
        auto end{context.end()};
        if (it == end || *it == '}') {
            return it;
        }
        ++it;
        if (it != end && *it != '}') {
            throw format_error("Invalid format specifier for Error");
        }
        return it;
    }

    auto format(const kosio::Error &error, auto &context) const noexcept {
        return format_to(context.out(), "{} (error {})", error.message(), error.value());
    }
};
} // namespace std