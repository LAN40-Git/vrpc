#pragma once
#include <string>
#include "vrpc/net/pb/status.hpp"
#include "vrpc/net/pb/detail/channel.hpp"

namespace vrpc {
class RpcMessage {
public:
    uint64_t    seq;               // 消息序号
    uint32_t    service_name_len;  // 服务名长度
    std::string service_name;      // 服务名
    uint32_t    method_name_len;   // 方法名长度
    std::string method_name;       // 方法名
    uint32_t    payload_size;      // protobuf 消息体长度
    std::string payload;           // protobuf 消息体
    uint8_t     status_code;       // RPC 状态码
    int32_t     check_sum;         // 校验和

public:
    void encode_check_sum() {
        uint32_t crc = 0;
        crc = crc32(crc, &seq, sizeof(seq));
        crc = crc32(crc, &service_name_len, sizeof(service_name_len));
        crc = crc32(crc, service_name.data(), service_name_len);
        crc = crc32(crc, &method_name_len, sizeof(method_name_len));
        crc = crc32(crc, method_name.data(), method_name_len);
        crc = crc32(crc, &payload_size, sizeof(payload_size));
        crc = crc32(crc, payload.data(), payload_size);
        crc = crc32(crc, &status_code, sizeof(status_code));
        check_sum = static_cast<int32_t>(crc);
    }

    bool verify_check_sum() const {
        uint32_t crc = 0;
        crc = crc32(crc, &seq, sizeof(seq));
        crc = crc32(crc, &service_name_len, sizeof(service_name_len));
        crc = crc32(crc, service_name.data(), service_name_len);
        crc = crc32(crc, &method_name_len, sizeof(method_name_len));
        crc = crc32(crc, method_name.data(), method_name_len);
        crc = crc32(crc, &payload_size, sizeof(payload_size));
        crc = crc32(crc, payload.data(), payload_size);
        crc = crc32(crc, &status_code, sizeof(status_code));
        return static_cast<int32_t>(crc) == check_sum;
    }

private:
    static uint32_t crc32(uint32_t crc, const void* data, size_t len) {
        crc = ~crc;
        const uint8_t* d = static_cast<const uint8_t*>(data);
        while (len--) {
            crc ^= *d++;
            for (int i = 0; i < 8; i++) {
                crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
            }
        }
        return ~crc;
    }
};
} // namespace vrpc