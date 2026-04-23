#pragma once
#include <string>
#include <optional>
#include <google/protobuf/message.h>
#include "vrpc/common/util/crc32.hpp"
#include "vrpc/common/util/sequence.hpp"

namespace vrpc::detail {
constexpr uint32_t MAX_RPC_MESSAGE_SIZE = 4 * 1024 * 1024;
struct RpcMessageHeader {
    uint32_t msg_size{}; // 报文总大小
};

/**
 * RPC 请求报文
 * 请使用 make 或 parse_prom 构造
 */
class RpcRequestMessage {
public:
    RpcRequestMessage() = default;

private:
    explicit RpcRequestMessage(std::string_view service_name, std::string_view method_name, std::string&& payload)
        : seq_(util::generate_sequence())
        , service_name_size_(service_name.size())
        , service_name_(service_name)
        , method_name_size_(method_name.size())
        , method_name_(method_name)
        , payload_size_(payload.size())
        , payload_(std::move(payload)) {
        encode_check_sum();
    }

    explicit RpcRequestMessage(uint64_t seq, std::string&& service_name, std::string&& method_name, std::string&& payload)
        : seq_(seq)
        , service_name_size_(service_name.size())
        , service_name_(std::move(service_name))
        , method_name_size_(method_name.size())
        , method_name_(std::move(method_name))
        , payload_size_(payload.size())
        , payload_(std::move(payload)) {
        encode_check_sum();
    }

public:
    RpcRequestMessage(const RpcRequestMessage&) = default;
    auto operator=(const RpcRequestMessage&) -> RpcRequestMessage& = default;

    RpcRequestMessage(RpcRequestMessage&&) = default;
    auto operator=(RpcRequestMessage&&) -> RpcRequestMessage& = default;

public:
    [[nodiscard]]
    static auto make(std::string_view service_name, std::string_view method_name, std::string&& payload) -> RpcRequestMessage {
        return RpcRequestMessage{service_name, method_name, std::move(payload)};
    }

    [[nodiscard]]
    static auto make(uint64_t seq, std::string&& service_name, std::string&& method_name, std::string&& payload) -> RpcRequestMessage {
        return RpcRequestMessage{seq, std::move(service_name), std::move(method_name), std::move(payload)};
    }

public:
    [[nodiscard]]
    static auto parse_from(const void* data, uint32_t size) -> std::optional<RpcRequestMessage> {
        if (data == nullptr || size < MIN_MESSAGE_SIZE) {
            return std::nullopt;
        }

        auto* ptr = static_cast<const char*>(data);
        uint32_t read_bytes = 0;

        // 读取报文序号
        auto seq = be64toh(*reinterpret_cast<const uint64_t*>(ptr));
        read_bytes += sizeof(seq);
        ptr += sizeof(seq);

        // 读取服务名大小
        auto service_name_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(service_name_size) + service_name_size;
        if (read_bytes > size) {
            return std::nullopt;
        }
        ptr += sizeof(service_name_size);

        // 读取服务名
        auto service_name = std::string{ptr, service_name_size};
        ptr += service_name_size;

        // 读取方法名大小
        auto method_name_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(method_name_size) + method_name_size;
        if (read_bytes > size) {
            return std::nullopt;
        }
        ptr += sizeof(method_name_size);

        // 读取方法名
        auto method_name = std::string{ptr, method_name_size};
        ptr += method_name_size;

        // 读取 protobuf 消息大小
        auto payload_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(payload_size) + payload_size;
        if (read_bytes > size) {
            return std::nullopt;
        }
        ptr += sizeof(payload_size);

        // 读取 protobuf 消息
        auto payload = std::string{ptr, payload_size};
        ptr += payload_size;

        // 读取校验和
        auto check_sum = be32toh(*reinterpret_cast<const uint32_t*>(ptr));

        auto message = make(seq, std::move(service_name), std::move(method_name), std::move(payload));

        if (!message.verify_check_sum(check_sum)) {
            return std::nullopt;
        }

        return message;
    }

public:
    [[nodiscard]]
    auto bytes_size() const -> uint32_t {
        return MIN_MESSAGE_SIZE + service_name_.size() + method_name_.size() + payload_.size();
    }

    void htobe() {
        seq_ = htobe64(seq_);
        service_name_size_ = htobe32(service_name_size_); // 服务名大小
        method_name_size_ = htobe32(method_name_size_);
        payload_size_ = htobe32(payload_size_);
        check_sum_ = htobe32(check_sum_);
    }

private:
    void encode_check_sum() {
        using util::crc32;
        uint32_t crc = 0;
        crc = crc32(crc, &seq_, sizeof(seq_));
        crc = crc32(crc, &service_name_size_, sizeof(service_name_size_));
        crc = crc32(crc, service_name_.data(), service_name_.size());
        crc = crc32(crc, &method_name_size_, sizeof(method_name_size_));
        crc = crc32(crc, method_name_.data(), method_name_.size());
        crc = crc32(crc, &payload_size_, sizeof(payload_size_));
        crc = crc32(crc, payload_.data(), payload_.size());
        check_sum_ = crc;
    }

    [[nodiscard]]
    auto verify_check_sum(uint32_t check_sum) const -> bool {
        return check_sum_ == check_sum;
    }

public:
    uint64_t    seq_{};                // 报文序号，回复与请求通用
    uint32_t    service_name_size_{0}; // 服务名大小
    std::string service_name_;         // 服务名
    uint32_t    method_name_size_{0};  // 方法名大小
    std::string method_name_;          // 方法名
    uint32_t    payload_size_{0};      // protobuf 消息大小
    std::string payload_;              // protobuf 消息
    uint32_t    check_sum_{0};         // 校验和
    static constexpr uint32_t MIN_MESSAGE_SIZE = sizeof(seq_) + sizeof(service_name_size_) + sizeof(method_name_size_) + sizeof(payload_size_) + sizeof(check_sum_);
};


/**
 * RPC 回复报文
 * 请使用 make 或 parse_prom 构造
 */
class RpcResponseMessage {
private:
    RpcResponseMessage() = default;

    explicit RpcResponseMessage(uint64_t seq, uint8_t status_code, std::string&& err_msg, std::string&& payload)
        : seq_(seq)
        , status_code_(status_code)
        , err_msg_size_(err_msg.size())
        , err_msg_(std::move(err_msg))
        , payload_size_(payload.size())
        , payload_(std::move(payload)) {
        encode_check_sum();
    }

    explicit RpcResponseMessage(uint64_t seq, const Status& status, std::string&& payload)
        : seq_(seq)
        , status_code_(status.code())
        , err_msg_size_(status.message().size())
        , err_msg_(std::string{status.message()})
        , payload_size_(payload.size())
        , payload_(std::move(payload)) {
        encode_check_sum();
    }

public:
    RpcResponseMessage(const RpcResponseMessage&) = default;
    auto operator=(const RpcResponseMessage&) -> RpcResponseMessage& = default;

    RpcResponseMessage(RpcResponseMessage&&) = default;
    auto operator=(RpcResponseMessage&&) -> RpcResponseMessage& = default;

public:
    [[nodiscard]]
    auto bytes_size() const -> uint32_t {
        return MIN_MESSAGE_SIZE + err_msg_size_ + payload_size_;
    }

    void htobe() {
        seq_ = htobe64(seq_);
        err_msg_size_ = htobe32(err_msg_size_);
        payload_size_ = htobe32(payload_size_);
        check_sum_ = htobe32(check_sum_);
    }

public:
    [[nodiscard]]
    static auto make(uint64_t seq, uint8_t status_code, std::string&& err_msg = "", std::string&& payload = "") -> RpcResponseMessage {
        return RpcResponseMessage{seq, status_code, std::move(err_msg), std::move(payload)};
    }

    [[nodiscard]]
    static auto make(uint64_t seq, const Status& status, std::string&& payload = "") -> RpcResponseMessage {
        return RpcResponseMessage{seq, status, std::move(payload)};
    }

    [[nodiscard]]
    static auto parse_from(const void* data, uint32_t size) -> std::optional<RpcResponseMessage> {
        if (data == nullptr || size < MIN_MESSAGE_SIZE) {
            return std::nullopt;
        }

        auto* ptr = static_cast<const char*>(data);
        uint32_t read_bytes = 0;

        // 读取报文序号
        auto seq = be64toh(*reinterpret_cast<const uint64_t*>(ptr));
        read_bytes += sizeof(seq);
        ptr += sizeof(seq);

        // 读取状态码
        auto status_code = *reinterpret_cast<const uint8_t*>(ptr);
        read_bytes += sizeof(status_code);
        ptr += sizeof(status_code);

        // 读取错误消息大小
        auto err_msg_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(err_msg_size) + err_msg_size;
        if (read_bytes > size) {
            return std::nullopt;
        }
        ptr += sizeof(err_msg_size);

        // 读取错误消息
        auto err_msg = std::string{ptr, err_msg_size};
        ptr += err_msg_size;

        // 读取 protobuf 消息大小
        auto payload_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(payload_size) + payload_size;
        if (read_bytes > size) {
            return std::nullopt;
        }
        ptr += sizeof(payload_size);

        // 读取 protobuf 消息
        auto payload = std::string{ptr, payload_size};
        ptr += payload_size;

        // 读取校验和
        auto check_sum = be32toh(*reinterpret_cast<const uint32_t*>(ptr));

        auto message = make(seq, status_code, std::move(err_msg), std::move(payload));

        if (!message.verify_check_sum(check_sum)) {
            return std::nullopt;
        }

        return message;
    }

private:
    void encode_check_sum() {
        using util::crc32;
        uint32_t crc = 0;
        crc = crc32(crc, &seq_, sizeof(seq_));
        crc = crc32(crc, &status_code_, sizeof(status_code_));
        crc = crc32(crc, &err_msg_size_, sizeof(err_msg_size_));
        crc = crc32(crc, err_msg_.data(), err_msg_.size());
        crc = crc32(crc, &payload_size_, sizeof(payload_size_));
        crc = crc32(crc, payload_.data(), payload_.size());
        check_sum_ = crc;
    }

    [[nodiscard]]
    auto verify_check_sum(uint32_t check_sum) const -> bool {
        return check_sum_ == check_sum;
    }

public:
    uint64_t    seq_{};           // 报文序号，回复与请求通用
    uint8_t     status_code_{0};  // RPC 状态码
    uint32_t    err_msg_size_{0}; // 错误消息长度
    std::string err_msg_;         // 错误消息（状态码非 0 时设置）
    uint32_t    payload_size_{0}; // protobuf 消息大小
    std::string payload_;         // protobuf 消息
    uint32_t    check_sum_{0};    // 校验和
    static constexpr uint32_t MIN_MESSAGE_SIZE = sizeof(seq_) + sizeof(status_code_)+ sizeof(err_msg_size_) + sizeof(payload_size_) + sizeof(check_sum_);
};
} // namespace vrpc::detail