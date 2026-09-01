#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

/** Constant-time compare of two 16-byte secrets. */
inline bool secretsEqual16(const uint8_t *a, const uint8_t *b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < 16; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0U;
}

/** Encode 16 raw bytes to 32 lowercase hex chars + NUL (outHex33 must be >= 33). */
inline void hexEncode16(const uint8_t *in, char *outHex33) {
    if (in == nullptr || outHex33 == nullptr) {
        return;
    }
    static const char *kHex = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        outHex33[i * 2] = kHex[in[i] >> 4];
        outHex33[i * 2 + 1] = kHex[in[i] & 0x0f];
    }
    outHex33[32] = '\0';
}

/** Decode exactly 32 hex chars into 16 bytes. */
inline bool hexDecode32Strict(const char *hex, uint8_t out16[16]) {
    if (hex == nullptr || out16 == nullptr || std::strlen(hex) != 32U) {
        return false;
    }
    for (size_t i = 0; i < 16; ++i) {
        char buf[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char *endPtr = nullptr;
        const unsigned long v = std::strtoul(buf, &endPtr, 16);
        if (endPtr != buf + 2 || v > 255) {
            return false;
        }
        out16[i] = static_cast<uint8_t>(v);
    }
    return true;
}
