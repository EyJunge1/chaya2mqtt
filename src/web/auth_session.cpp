#include <Arduino.h>

#include "auth.h"

#include "auth_config.h"
#include "auth_internal.h"

#include "config/app_config.h"
#include "wifi/wlan.h"

#include "util/time_helpers.h"

#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_random.h>

portMUX_TYPE s_authMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t      s_csrfTokenRaw[16]{};

bool          s_sessionActive    = false;
uint8_t       s_sessionRaw[16]{};
unsigned long s_sessionCreatedMs = 0;

std::atomic<unsigned>      s_authFailStreak{0};
std::atomic<unsigned long> s_authLockoutStartMs{0};

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

void rotateCsrfTokenLocked() {
    esp_fill_random(s_csrfTokenRaw, sizeof(s_csrfTokenRaw));
}

void hexEncode16(const uint8_t* in, char* outHex65) {
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        outHex65[i * 2]     = kHex[in[i] >> 4];
        outHex65[i * 2 + 1] = kHex[in[i] & 0x0f];
    }
    outHex65[32] = '\0';
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

static bool csrfTokenMatches(const char* submitted) {
    if (submitted == nullptr) {
        return false;
    }
    portENTER_CRITICAL(&s_authMux);
    uint8_t expectedCopy[16];
    memcpy(expectedCopy, s_csrfTokenRaw, sizeof(expectedCopy));
    portEXIT_CRITICAL(&s_authMux);
    if (strlen(submitted) != 32U) {
        return false;
    }
    uint8_t got[16];
    return hexDecode32Strict(submitted, got) && secretsEqual16(got, expectedCopy);
}

static bool parseCookieSession(AsyncWebServerRequest* req, uint8_t outRaw[16]) {
    if (!req->hasHeader("Cookie")) {
        return false;
    }
    const String& c   = req->header("Cookie");
    const char*   key = "chaya_sid=";
    const int     idx = c.indexOf(key);
    if (idx < 0) {
        return false;
    }
    const int start  = idx + static_cast<int>(strlen(key));
    const int end    = c.indexOf(';', start);
    const int segEnd = (end < 0) ? c.length() : end;
    if ((segEnd - start) != 32) {
        return false;
    }
    char tok[33];
    for (int i = 0; i < 32; ++i) {
        tok[i] = static_cast<char>(c[start + i]);
    }
    tok[32] = '\0';
    return hexDecode32Strict(tok, outRaw);
}

static bool sessionMatchesRequest(AsyncWebServerRequest* req) {
    portENTER_CRITICAL(&s_authMux);
    const bool active = s_sessionActive;
    uint8_t    sessionCopy[16];
    const unsigned long createdMs = s_sessionCreatedMs;
    if (active) {
        memcpy(sessionCopy, s_sessionRaw, 16);
    }
    portEXIT_CRITICAL(&s_authMux);

    if (!active) {
        return false;
    }
    const unsigned long nowMs = millis();
    if (deadlineReached(createdMs, kSessionCookieMaxAgeSec * 1000UL, nowMs)) {
        webAuthInvalidateSession();
        return false;
    }
    uint8_t cookieRaw[16];
    if (!parseCookieSession(req, cookieRaw)) {
        return false;
    }
    return secretsEqual16(cookieRaw, sessionCopy);
}

void webAuthInit() {
    portENTER_CRITICAL(&s_authMux);
    rotateCsrfTokenLocked();
    portEXIT_CRITICAL(&s_authMux);
}

void webAuthGetCsrfTokenHex(char* outHex33, size_t outLen) {
    if (outHex33 == nullptr || outLen < 33U) {
        return;
    }
    portENTER_CRITICAL(&s_authMux);
    hexEncode16(s_csrfTokenRaw, outHex33);
    portEXIT_CRITICAL(&s_authMux);
}

bool webAuthValidateCsrfPost(AsyncWebServerRequest* req) {
    if (!req->hasParam("csrf_token", true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam("csrf_token", true);
    if (p == nullptr) {
        return false;
    }
    return csrfTokenMatches(p->value().c_str());
}

bool webAuthIsAuthenticated(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        return true;
    }
    return sessionMatchesRequest(req);
}

void webAuthInvalidateSession() {
    portENTER_CRITICAL(&s_authMux);
    s_sessionActive    = false;
    s_sessionCreatedMs = 0;
    memset(s_sessionRaw, 0, sizeof(s_sessionRaw));
    portEXIT_CRITICAL(&s_authMux);
}
