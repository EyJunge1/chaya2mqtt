#include "csrf.h"

#include "csrf_pure.h"
#include "hex_codec.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_random.h>
#include <freertos/portmacro.h>

namespace {

portMUX_TYPE s_csrfMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t s_csrfCurrentRaw[16]{};
uint8_t s_csrfPreviousRaw[16]{};
uint32_t s_csrfIssuedAtMs = 0;
uint32_t s_csrfRotatedAtMs = 0;
bool s_csrfHasPrevious = false;

void csrfRotateIfNeededLocked(uint32_t nowMs) {
    if (!csrfTokenNeedsRotation(nowMs, s_csrfIssuedAtMs)) {
        return;
    }
    memcpy(s_csrfPreviousRaw, s_csrfCurrentRaw, sizeof(s_csrfPreviousRaw));
    esp_fill_random(s_csrfCurrentRaw, sizeof(s_csrfCurrentRaw));
    s_csrfIssuedAtMs = nowMs;
    s_csrfRotatedAtMs = nowMs;
    s_csrfHasPrevious = true;
}

bool csrfTokenMatches(const char *submitted) {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    portENTER_CRITICAL(&s_csrfMux);
    csrfRotateIfNeededLocked(nowMs);
    uint8_t currentCopy[16];
    uint8_t previousCopy[16];
    memcpy(currentCopy, s_csrfCurrentRaw, sizeof(currentCopy));
    memcpy(previousCopy, s_csrfPreviousRaw, sizeof(previousCopy));
    const bool previousAllowed = s_csrfHasPrevious && csrfPreviousTokenAllowed(nowMs, s_csrfRotatedAtMs);
    portEXIT_CRITICAL(&s_csrfMux);
    const bool currentMatch = csrfSubmittedMatchesExpected(submitted, currentCopy);
    const bool previousMatch = csrfSubmittedMatchesExpected(submitted, previousCopy);
    return csrfAcceptSubmitted(currentMatch, previousMatch, previousAllowed);
}

} // namespace

void webCsrfInit() {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    portENTER_CRITICAL(&s_csrfMux);
    esp_fill_random(s_csrfCurrentRaw, sizeof(s_csrfCurrentRaw));
    memset(s_csrfPreviousRaw, 0, sizeof(s_csrfPreviousRaw));
    s_csrfIssuedAtMs = nowMs;
    s_csrfRotatedAtMs = nowMs;
    s_csrfHasPrevious = false;
    portEXIT_CRITICAL(&s_csrfMux);
}

void webCsrfGetTokenHex(char *outHex33, size_t outLen, uint32_t *outExpiresInSeconds) {
    if (outHex33 == nullptr || outLen < 33U) {
        return;
    }
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    portENTER_CRITICAL(&s_csrfMux);
    csrfRotateIfNeededLocked(nowMs);
    hexEncode16(s_csrfCurrentRaw, outHex33);
    const uint32_t elapsed = static_cast<uint32_t>(nowMs - s_csrfIssuedAtMs);
    const uint32_t remaining = elapsed < kCsrfTokenTtlMs ? kCsrfTokenTtlMs - elapsed : 0U;
    portEXIT_CRITICAL(&s_csrfMux);
    if (outExpiresInSeconds != nullptr) {
        *outExpiresInSeconds = (remaining + 999U) / 1000U;
    }
}

bool webCsrfValidatePost(AsyncWebServerRequest *req) {
    // Header only — query-string and body tokens are rejected (SEC-07).
    if (req == nullptr || !req->hasHeader(kCsrfHeaderName)) {
        return false;
    }
    return csrfTokenMatches(req->header(kCsrfHeaderName).c_str());
}
