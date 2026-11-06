#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace ewc {

inline const std::array<uint32_t, 256>& crc32_table() {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t r = i;
            for (int k = 0; k < 8; ++k) r = (r & 1) ? (0xEDB88320u ^ (r >> 1)) : (r >> 1);
            a[i] = r;
        }
        return a;
    }();
    return t;
}

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    const auto& table = crc32_table();
    c = ~c;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

// CRC-16/CCITT-FALSE (table driven)
inline const std::array<uint16_t, 256>& crc16_table() {
    static const std::array<uint16_t, 256> t = [] {
        std::array<uint16_t, 256> a{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint16_t c = uint16_t(i << 8);
            for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
            a[i] = c;
        }
        return a;
    }();
    return t;
}

inline uint16_t crc16(const uint8_t* p, size_t n) {
    const auto& table = crc16_table();
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; ++i) c = uint16_t((c << 8) ^ table[((c >> 8) ^ p[i]) & 0xFF]);
    return c;
}

}  // namespace ewc
