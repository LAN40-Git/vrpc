#pragma once
#include <cstring>
#include <string>
#include "vrpc/common/util/crc32.hpp"
#include "vrpc/common/util/sequence.hpp"

namespace vrpc::detail {
constexpr uint32_t MAX_RPC_MESSAGE_SIZE = 4 * 1024 * 1024;

struct RpcMessageHeader {
    uint32_t msg_size; // 报文总大小
};

class RpcRequestMessage {
public:
    RpcRequestMessage() {
        seq = util::generate_sequence();
    }

public:
    uint64_t    seq;                  // 报文序号，回复与请求通用
    uint32_t    service_name_size{0}; // 服务名大小
    std::string service_name;         // 服务名
    uint32_t    method_name_size{0};  // 方法名大小
    std::string method_name;          // 方法名
    uint32_t    payload_size{0};      // protobuf 消息大小
    std::string payload;              // protobuf 消息
    uint32_t    check_sum{0};         // 校验和

    static constexpr uint32_t MIN_MESSAGE_SIZE = sizeof(service_name_size) + sizeof(method_name_size) + sizeof(payload_size) + sizeof(check_sum);

public:
    void set_service_name(std::string&& name) {
        service_name = std::move(name);
        service_name_size = service_name.size();
    }

    void set_method_name(std::string&& name) {
        method_name = std::move(name);
        method_name_size = method_name.size();
    }

    void set_payload(std::string&& pd) {
        payload = std::move(pd);
        payload_size = payload.size();
    }

public:
    [[nodiscard]]
    auto serialize_to(void* data, uint32_t size) -> bool {
        if (data == nullptr || size < bytes_size()) {
            return false;
        }

        // 生成校验和
        encode_check_sum();

        auto* ptr = static_cast<char*>(data);

        // 写入服务名大小
        *reinterpret_cast<uint32_t*>(ptr) = htobe32(service_name_size);
        ptr += sizeof(service_name_size);

        // 写入服务名
        std::memcpy(ptr, service_name.data(), service_name.size());
        ptr += service_name.size();

        // 写入方法名大小
        *reinterpret_cast<uint32_t*>(ptr) = htobe32(method_name_size);
        ptr += sizeof(method_name_size);

        // 写入方法名
        std::memcpy(ptr, method_name.data(), method_name.size());
        ptr += method_name.size();

        // 写入 protobuf 消息大小
        *reinterpret_cast<uint32_t*>(ptr) = htobe32(payload_size);
        ptr += sizeof(payload_size);

        // 写入 protobuf 消息
        std::memcpy(ptr, payload.data(), payload.size());
        ptr += payload.size();

        // 写入校验和
        *reinterpret_cast<uint32_t*>(ptr) = htobe32(check_sum);
        return true;
    }

    [[nodiscard]]
    auto parse_from(const void* data, uint32_t size) -> bool {
        if (data == nullptr || size < MIN_MESSAGE_SIZE) {
            return false;
        }

        auto* ptr = static_cast<const char*>(data);
        uint32_t read_bytes = 0;

        // 读取服务名大小
        service_name_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(service_name_size) + service_name_size;
        if (read_bytes > size) {
            return false;
        }
        ptr += sizeof(service_name_size);

        // 读取服务名
        service_name = std::string{ptr, service_name_size};
        ptr += service_name_size;

        // 读取方法名大小
        method_name_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(method_name_size) + method_name_size;
        if (read_bytes > size) {
            return false;
        }
        ptr += sizeof(method_name_size);

        // 读取方法名
        method_name = std::string{ptr, method_name_size};
        ptr += method_name_size;

        // 读取 protobuf 消息大小
        payload_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(payload_size) + payload_size;
        if (read_bytes > size) {
            return false;
        }
        ptr += sizeof(payload_size);

        // 读取 protobuf 消息
        payload = std::string{ptr, payload_size};
        ptr += payload_size;

        // 读取校验和
        check_sum = be32toh(*reinterpret_cast<const uint32_t*>(ptr));

        return verify_check_sum();
    }

    [[nodiscard]]
    auto bytes_size() const -> uint32_t {
        return MIN_MESSAGE_SIZE + service_name.size() + method_name.size() + payload.size();
    }

private:
    void encode_check_sum() {
        using util::crc32;
        uint32_t crc = 0;
        crc = crc32(crc, &service_name_size, sizeof(service_name_size));
        crc = crc32(crc, service_name.data(), service_name.size());
        crc = crc32(crc, &method_name_size, sizeof(method_name_size));
        crc = crc32(crc, method_name.data(), method_name.size());
        crc = crc32(crc, &payload_size, sizeof(payload_size));
        crc = crc32(crc, payload.data(), payload.size());
        check_sum = crc;
    }

    [[nodiscard]]
    auto verify_check_sum() const -> bool {
        using util::crc32;
        uint32_t crc = 0;
        crc = crc32(crc, &service_name_size, sizeof(service_name_size));
        crc = crc32(crc, service_name.data(), service_name_size);
        crc = crc32(crc, &method_name_size, sizeof(method_name_size));
        crc = crc32(crc, method_name.data(), method_name_size);
        crc = crc32(crc, &payload_size, sizeof(payload_size));
        crc = crc32(crc, payload.data(), payload.size());
        return crc == check_sum;
    }
};

class RpcResponseMessage {
public:
    RpcResponseMessage(uint64_t seq)
        : seq(seq) {}

public:
    uint64_t    seq;             // 报文序号，回复与请求通用
    uint8_t     status_code{0};  // RPC 状态码
    uint32_t    err_msg_size{0}; // 错误消息长度
    std::string err_msg;         // 错误消息（状态码非 0 时设置）
    uint32_t    payload_size{0}; // protobuf 消息大小
    std::string payload;         // protobuf 消息
    uint32_t    check_sum{0};    // 校验和

    static constexpr uint32_t MIN_MESSAGE_SIZE = sizeof(status_code)+ sizeof(err_msg_size) + sizeof(payload_size) + sizeof(check_sum);

public:
    void set_err_msg(std::string&& msg) {
        err_msg = std::move(msg);
        err_msg_size = err_msg.size();
    }

    void set_payload(std::string&& pd) {
        payload = std::move(pd);
        payload_size = payload.size();
    }

public:
    [[nodiscard]]
    auto serialize_to(void* data, uint32_t size) -> bool {
        if (data == nullptr || size < bytes_size()) {
            return false;
        }

        // 生成校验和
        encode_check_sum();

        auto* ptr = static_cast<char*>(data);

        // 写入状态码
        *reinterpret_cast<uint8_t*>(ptr) = status_code;
        ptr += sizeof(status_code);

        // 写入错误消息大小
        *reinterpret_cast<uint32_t*>(ptr) = htobe32(err_msg_size);
        ptr += sizeof(err_msg_size);

        // 写入错误消息
        std::memcpy(ptr, err_msg.data(), err_msg.size());
        ptr += err_msg.size();

        // 写入 protobuf 消息大小
        *reinterpret_cast<uint32_t*>(ptr) = htobe32(payload_size);
        ptr += sizeof(payload_size);

        // 写入 protobuf 消息
        std::memcpy(ptr, payload.data(), payload.size());
        ptr += payload.size();

        // 写入校验和
        *reinterpret_cast<uint32_t*>(ptr) = htobe32(check_sum);
        return true;
    }

    [[nodiscard]]
    auto parse_from(const void* data, uint32_t size) -> bool {
        if (data == nullptr || size < MIN_MESSAGE_SIZE) {
            return false;
        }

        auto* ptr = static_cast<const char*>(data);
        uint32_t read_bytes = 0;

        // 读取状态码
        status_code = *reinterpret_cast<const uint8_t*>(ptr);
        read_bytes += sizeof(status_code);
        if (read_bytes > size) {
            return false;
        }
        ptr += sizeof(status_code);

        // 读取错误消息大小
        err_msg_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(err_msg_size) + err_msg_size;
        if (read_bytes > size) {
            return false;
        }
        ptr += sizeof(err_msg_size);

        // 读取错误消息
        err_msg = std::string{ptr, err_msg_size};
        ptr += err_msg_size;

        // 读取 protobuf 消息大小
        payload_size = be32toh(*reinterpret_cast<const uint32_t*>(ptr));
        read_bytes += sizeof(payload_size) + payload_size;
        if (read_bytes > size) {
            return false;
        }
        ptr += sizeof(payload_size);

        // 读取 protobuf 消息
        payload = std::string{ptr, payload_size};
        ptr += payload_size;

        // 读取校验和
        check_sum = be32toh(*reinterpret_cast<const uint32_t*>(ptr));

        return verify_check_sum();
    }

    [[nodiscard]]
    auto bytes_size() const -> uint32_t {
        return MIN_MESSAGE_SIZE + err_msg_size + payload_size;
    }

private:
    void encode_check_sum() {
        using util::crc32;
        uint32_t crc = 0;
        crc = crc32(crc, &status_code, sizeof(status_code));
        crc = crc32(crc, &err_msg_size, sizeof(err_msg_size));
        crc = crc32(crc, err_msg.data(), err_msg.size());
        crc = crc32(crc, &payload_size, sizeof(payload_size));
        crc = crc32(crc, payload.data(), payload.size());
        check_sum = crc;
    }

    [[nodiscard]]
    auto verify_check_sum() const -> bool {
        using util::crc32;
        uint32_t crc = 0;
        crc = crc32(crc, &status_code, sizeof(status_code));
        crc = crc32(crc, &err_msg_size, sizeof(err_msg_size));
        crc = crc32(crc, err_msg.data(), err_msg.size());
        crc = crc32(crc, &payload_size, sizeof(payload_size));
        crc = crc32(crc, payload.data(), payload.size());
        return crc == check_sum;
    }
};
} // namespace vrpc