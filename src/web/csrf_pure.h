#pragma once

#include <cstdint>
#include <cstring>

#include "hex_codec.h"

constexpr uint32_t kCsrfTokenTtlMs   = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kCsrfTokenGraceMs = 5UL * 60UL * 1000UL;

inline bool csrfTokenNeedsRotation(uint32_t nowMs, uint32_t issuedAtMs) {
    return static_cast<uint32_t>(nowMs - issuedAtMs) >= kCsrfTokenTtlMs;
}

inline bool csrfPreviousTokenAllowed(uint32_t nowMs, uint32_t rotatedAtMs) {
    return static_cast<uint32_t>(nowMs - rotatedAtMs) < kCsrfTokenGraceMs;
}

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

/** Current match OR (grace active AND previous match). */
inline bool csrfAcceptSubmitted(bool currentMatch, bool previousMatch, bool previousAllowed) {
    return currentMatch || (previousAllowed && previousMatch);
}
