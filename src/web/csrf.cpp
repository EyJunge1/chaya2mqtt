#include "csrf.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_random.h>
#include <freertos/portmacro.h>

namespace {

portMUX_TYPE s_csrfMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t      s_csrfTokenRaw[16]{};

bool secretsEqual16(const uint8_t* a, const uint8_t* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < 16; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0U;
}

void hexEncode16(const uint8_t* in, char* outHex33) {
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        outHex33[i * 2]     = kHex[in[i] >> 4];
        outHex33[i * 2 + 1] = kHex[in[i] & 0x0f];
    }
    outHex33[32] = '\0';
}

bool hexDecode32Strict(const char* hex, uint8_t out16[16]) {
    if (hex == nullptr || strlen(hex) != 32U) {
        return false;
    }
    for (size_t i = 0; i < 16; ++i) {
        char              buf[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char*             endPtr = nullptr;
        const unsigned long v    = strtoul(buf, &endPtr, 16);
        if (endPtr != buf + 2 || v > 255) {
            return false;
        }
        out16[i] = static_cast<uint8_t>(v);
    }
    return true;
}

bool csrfTokenMatches(const char* submitted) {
    if (submitted == nullptr) {
        return false;
    }
    portENTER_CRITICAL(&s_csrfMux);
    uint8_t expectedCopy[16];
    memcpy(expectedCopy, s_csrfTokenRaw, sizeof(expectedCopy));
    portEXIT_CRITICAL(&s_csrfMux);
    if (strlen(submitted) != 32U) {
        return false;
    }
    uint8_t got[16];
    return hexDecode32Strict(submitted, got) && secretsEqual16(got, expectedCopy);
}

} // namespace

void webCsrfInit() {
    portENTER_CRITICAL(&s_csrfMux);
    esp_fill_random(s_csrfTokenRaw, sizeof(s_csrfTokenRaw));
    portEXIT_CRITICAL(&s_csrfMux);
}

void webCsrfGetTokenHex(char* outHex33, size_t outLen) {
    if (outHex33 == nullptr || outLen < 33U) {
        return;
    }
    portENTER_CRITICAL(&s_csrfMux);
    hexEncode16(s_csrfTokenRaw, outHex33);
    portEXIT_CRITICAL(&s_csrfMux);
}

bool webCsrfValidatePost(AsyncWebServerRequest* req) {
    if (!req->hasParam("csrf_token", true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam("csrf_token", true);
    if (p == nullptr) {
        return false;
    }
    return csrfTokenMatches(p->value().c_str());
}
