#pragma once
#include <cstdint>

namespace vrpc::util {
[[nodiscard]]
    static auto crc32(uint32_t crc, const void* data, std::size_t len) -> uint32_t {
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
} // namespace vrpc::util