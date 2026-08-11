#pragma once

#include <cstring>

#include "hex_codec.h"

/**
 * Host-testable CSRF match: submitted form value must be exactly 32 hex chars
 * decoding to the expected 16-byte secret (constant-time compare).
 */
inline bool csrfSubmittedMatchesExpected(const char* submitted, const uint8_t expectedRaw[16]) {
    if (submitted == nullptr || expectedRaw == nullptr) {
        return false;
    }
    if (std::strlen(submitted) != 32U) {
        return false;
    }
    uint8_t got[16]{};
    return hexDecode32Strict(submitted, got) && secretsEqual16(got, expectedRaw);
}
