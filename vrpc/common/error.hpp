#pragma once
#include <expected>
#include <string_view>
#include <cstring>
#include <format>

namespace vrpc {
class Error {
public:
    enum Code {
        kRepeatedConnection = 0,
        kMaxCode
    };

public:
    explicit Error(int code) : code_(code) {}

public:
    [[nodiscard]]
    auto code() const noexcept -> int {
        return code_;
    }

    [[nodiscard]]
    auto message() const noexcept -> std::string_view {
        switch (code_) {
            case kRepeatedConnection:
                return "receive repeated connection";
            default:
                return "unknown error";
        }
    }

private:
    int code_;
};

template <typename T>
using Result = std::expected<T, Error>;

static auto make_error(int error_code) -> Error {
    return Error{error_code};
}
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

    auto format(const vrpc::Error &error, auto &context) const noexcept {
        return format_to(context.out(), "{} (error {})", error.message(), error.code());
    }
};
} // namespace std