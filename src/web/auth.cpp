#include "auth.h"

#include "config/app_config.h"
#include "hw/button.h"
#include "heart/counter.h"
#include "display/display.h"
#include "mqtt/config.h"
#include "pages.h"
#include "web_utils.h"
#include "wifi/wlan.h"

#include "util/time_helpers.h"
#include "util/auth_code_validation.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/portmacro.h>

#include "log_tag.h"

DEFINE_LOG_TAG("AUTH");

static constexpr unsigned long kChallengeTtlMs   = 300000UL; // 5 min
static constexpr unsigned long kConfirmWindowMs  = 10000UL; // wait for physical button
static constexpr unsigned long kAuthLockoutMs    = 3600000UL; // 1 hour after repeated failures
static constexpr unsigned kAuthFailsForLock      = 3;
static constexpr unsigned long kSessionCookieMaxAgeSec = 86400UL;
static constexpr const char    kCookieName[]     = "chaya_sid";

/** Protects CSRF + session (Async web task vs main webAuthLoop / button reads). */
static portMUX_TYPE s_authMux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t s_csrfTokenRaw[16]{};

static bool    s_sessionActive = false;
static uint8_t s_sessionRaw[16]{};
static unsigned long s_sessionCreatedMs = 0;

/** Challenge fields — visible across tasks without holding s_authMux. */
static std::atomic<uint32_t>        s_challengeCode{0};
static std::atomic<unsigned long>   s_challengeStartedMs{0};
static std::atomic<bool>            s_challengePending{false};

static std::atomic<bool>           s_awaitingButtonConfirm{false};
static std::atomic<unsigned long> s_confirmStartedMs{0};

static std::atomic<unsigned>       s_authFailStreak{0};
static std::atomic<unsigned long>  s_authLockoutStartMs{0};

static bool isPublicPath(const String& uri) {
    if (configIsApMode()) {
        if (uri == "/wifi" || uri.startsWith("/wifi-scan") || uri == "/wifi-status"
            || uri.startsWith("/wifi-connect")) {
            return true;
        }
        return uri == "/" || uri == "/favicon.ico";
    }
    if (uri.startsWith("/wifi-connect") || uri == "/wifi") {
        return true;
    }
    if (uri.startsWith("/auth") || uri == "/logout") {
        return true;
    }
    return uri == "/" || uri == "/favicon.ico";
}

static bool secretsEqual16(const uint8_t* a, const uint8_t* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < 16; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0U;
}

static void rotateCsrfTokenLocked() {
    esp_fill_random(s_csrfTokenRaw, sizeof(s_csrfTokenRaw));
}

static bool tryConsumeAuthChallenge(uint32_t entered, unsigned long nowMs) {
    if (!s_challengePending.load(std::memory_order_acquire)) {
        return false;
    }
    if (deadlineReached(s_challengeStartedMs.load(std::memory_order_acquire), kChallengeTtlMs,
                        nowMs)) {
        return false;
    }
    if (entered != s_challengeCode.load(std::memory_order_relaxed)) {
        return false;
    }
    bool expectedPending = true;
    if (!s_challengePending.compare_exchange_strong(expectedPending, false,
                                                    std::memory_order_acq_rel)) {
        return false;
    }
    s_challengeCode.store(0, std::memory_order_relaxed);
    s_challengeStartedMs.store(0, std::memory_order_relaxed);
    return true;
}

static bool authCodeSyntaxOk(const char* codeStr) {
    return webAuthCodeSyntaxOk(codeStr);
}

static void hexEncode16(const uint8_t* in, char* outHex65) {
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        outHex65[i * 2]     = kHex[in[i] >> 4];
        outHex65[i * 2 + 1] = kHex[in[i] & 0x0f];
    }
    outHex65[32] = '\0';
}

static bool hexDecode32Strict(const char* hex, uint8_t out16[16]) {
    if (hex == nullptr || strlen(hex) != 32U) {
        return false;
    }
    for (size_t i = 0; i < 16; ++i) {
        char buf[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char* endPtr = nullptr;
        const unsigned long v = strtoul(buf, &endPtr, 16);
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
    const String& c    = req->header("Cookie");
    const char*   key  = "chaya_sid=";
    const int     idx  = c.indexOf(key);
    if (idx < 0) {
        return false;
    }
    const int start   = idx + static_cast<int>(strlen(key));
    const int end     = c.indexOf(';', start);
    const int segEnd  = (end < 0) ? c.length() : end;
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

bool webAuthIsAuthenticated(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        return true;
    }
    return sessionMatchesRequest(req);
}

static bool isSafeNextPath(const char* next) {
    if (next == nullptr || next[0] != '/') {
        return false;
    }
    if (strstr(next, "..") != nullptr) {
        return false;
    }
    if (strcmp(next, "/logout") == 0) {
        return false;
    }
    return true;
}

/** Percent-encode path+query into out (ASCII). Returns false if outLen is insufficient. */
static bool urlEncodePathQueryCStr(const char* s, char* out, size_t outLen) {
    if (s == nullptr || out == nullptr || outLen == 0U) {
        return false;
    }
    size_t pos = 0;
    for (; *s != '\0'; ++s) {
        const unsigned char uch = static_cast<unsigned char>(*s);
        char        chunk[5];
        const char* piece;
        size_t      pieceLen;
        if ((uch >= 'a' && uch <= 'z') || (uch >= 'A' && uch <= 'Z') || (uch >= '0' && uch <= '9')
            || uch == '-' || uch == '_' || uch == '.' || uch == '/' || uch == '?'
            || uch == '=' || uch == '&') {
            chunk[0] = static_cast<char>(uch);
            chunk[1] = '\0';
            piece    = chunk;
            pieceLen = 1;
        } else {
            snprintf(chunk, sizeof(chunk), "%%%02X", uch);
            pieceLen = strlen(chunk);
            piece    = chunk;
        }
        if (pos + pieceLen + 1U > outLen) {
            return false;
        }
        memcpy(out + pos, piece, pieceLen);
        pos += pieceLen;
    }
    out[pos] = '\0';
    return true;
}

static void scheduleMainTaskScreenAfterAuthFlow() {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);
    if (cfg.server[0] != '\0' && !configIsApMode()) {
        requestDeferredDrawHeartScreen();
    } else {
        requestDeferredDrawSplashScreen();
    }
}

static void challengeClearAtomic() {
    s_challengePending.store(false, std::memory_order_release);
    s_challengeStartedMs.store(0, std::memory_order_relaxed);
    s_challengeCode.store(0, std::memory_order_relaxed);
}

static void awaitingClearAtomic() {
    s_awaitingButtonConfirm.store(false, std::memory_order_release);
    s_confirmStartedMs.store(0, std::memory_order_relaxed);
}

static void challengeBeginAtomic(uint32_t code, unsigned long startedMs) {
    s_challengeCode.store(code, std::memory_order_relaxed);
    s_challengeStartedMs.store(startedMs, std::memory_order_release);
    s_challengePending.store(true, std::memory_order_release);
}

static void authConfirmWindowExpired() {
    ESP_LOGI(TAG, "Auth button confirm window expired");
    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();
}

static void authChallengeExpired() {
    ESP_LOGI(TAG, "Web auth challenge code expired");
    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();
}

/** If idle, starts deferred prompt draw + LED blink after draw (see display). Skips restart if confirm or challenge already pending. */
static void maybeStartAwaitingButtonConfirm(bool resetFailStreakFromGet) {
    if (s_awaitingButtonConfirm.load(std::memory_order_acquire)
        || s_challengePending.load(std::memory_order_acquire)) {
        return;
    }
    bool expectedAwaiting = false;
    if (!s_awaitingButtonConfirm.compare_exchange_strong(expectedAwaiting, true,
                                                         std::memory_order_acq_rel)) {
        return;
    }
    if (s_challengePending.load(std::memory_order_acquire)) {
        s_awaitingButtonConfirm.store(false, std::memory_order_release);
        return;
    }

    s_confirmStartedMs.store(millis(), std::memory_order_release);

    if (resetFailStreakFromGet) {
        s_authFailStreak.store(0, std::memory_order_relaxed);
    }
    if (!requestDeferredDrawAuthPromptChecked()) {
        s_awaitingButtonConfirm.store(false, std::memory_order_release);
        ESP_LOGW(TAG, "Auth prompt enqueue failed (display queue full)");
    }
    /* Auth LED blink is started after drawAuthPrompt() completes — see display.cpp */
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

void webAuthHandleButtonDuringAuthBlink() {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        return;
    }
    if (!s_awaitingButtonConfirm.load(std::memory_order_acquire)) {
        return;
    }

    const unsigned long nowMs = millis();
    if (deadlineReached(s_confirmStartedMs.load(std::memory_order_acquire), kConfirmWindowMs,
                        nowMs)) {
        return;
    }

    awaitingClearAtomic();

    const uint32_t      code      = (esp_random() % 999999U) + 1U;
    challengeBeginAtomic(code, nowMs);
    requestDeferredDrawAuthCode(code);
    buttonSetAuthBlinkActive(false); /* Immediate feedback: drawing code (E-Ink blocks loop). */
}

void webAuthResetConfirmDeadline() {
    if (!s_awaitingButtonConfirm.load(std::memory_order_acquire)) {
        return;
    }
    s_confirmStartedMs.store(millis(), std::memory_order_release);
}

void webAuthLoop() {
    const unsigned long nowMs = millis();

    static uint32_t s_sessionEvictCounter = 0U;
    if (++s_sessionEvictCounter >= 120U) {
        s_sessionEvictCounter = 0U;
        portENTER_CRITICAL(&s_authMux);
        if (s_sessionActive
            && deadlineReached(s_sessionCreatedMs, kSessionCookieMaxAgeSec * 1000UL, nowMs)) {
            s_sessionActive    = false;
            s_sessionCreatedMs = 0;
            memset(s_sessionRaw, 0, sizeof(s_sessionRaw));
        }
        portEXIT_CRITICAL(&s_authMux);
    }

    if (s_awaitingButtonConfirm.load(std::memory_order_acquire)
        && buttonIsAuthBlinkActive()) {
        if (deadlineReached(s_confirmStartedMs.load(std::memory_order_acquire), kConfirmWindowMs,
                            nowMs)) {
            authConfirmWindowExpired();
            return;
        }
    }

    if (s_challengePending.load(std::memory_order_acquire)) {
        if (deadlineReached(s_challengeStartedMs.load(std::memory_order_acquire), kChallengeTtlMs,
                            nowMs)) {
            authChallengeExpired();
        }
    }
}

bool webAuthRedirectIfUnauthenticated(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        return false;
    }
    const String uri = req->url();
    if (isPublicPath(uri)) {
        return false;
    }
    if (webAuthIsAuthenticated(req)) {
        return false;
    }
    ESP_LOGD(TAG, "Redirect unauthenticated request → /auth (uri=%s)", uri.c_str());
    char nextEnc[256];
    char loc[320];
    if (!urlEncodePathQueryCStr(uri.c_str(), nextEnc, sizeof(nextEnc))) {
        webRedirect(req, F("/auth"));
        return true;
    }
    const int n = snprintf(loc, sizeof(loc), "/auth?next=%s", nextEnc);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(loc)) {
        webRedirect(req, F("/auth"));
        return true;
    }
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), loc);
    webAddSecurityHeaders(resp);
    req->send(resp);
    return true;
}

static void handleAuthGet(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        webRedirect(req, F("/"));
        return;
    }
    bool wrong = false;
    if (req->hasParam("bad", false)) {
        wrong = true;
    }
    maybeStartAwaitingButtonConfirm(/*resetFailStreak=*/false);
    streamAuthPage(req, wrong);
}

/** Remaining seconds until lockout ends (rounded up); 0 if not locked out. */
static unsigned lockoutRemainingSec(unsigned long nowMs) {
    const unsigned long start = s_authLockoutStartMs.load(std::memory_order_acquire);
    if (start == 0UL || deadlineReached(start, kAuthLockoutMs, nowMs)) {
        return 0U;
    }
    const uint32_t remMs = remainingMs(start, kAuthLockoutMs, nowMs);
    const unsigned long sec = (static_cast<unsigned long>(remMs) + 999UL) / 1000UL;
    return static_cast<unsigned>(
        std::min(sec, static_cast<unsigned long>(7200))); // sane upper bound if clock skew etc.
}

static std::atomic<unsigned long> s_lastLockoutNoticeMs{0};

static void handleAuthPost(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        webRedirect(req, F("/"));
        return;
    }

    const unsigned long nowMs = millis();
    const unsigned long lockoutStart = s_authLockoutStartMs.load(std::memory_order_acquire);
    if (lockoutStart != 0UL && !deadlineReached(lockoutStart, kAuthLockoutMs, nowMs)) {
        const unsigned long  remSec = lockoutRemainingSec(nowMs);
        const unsigned long  lastNotice =
            s_lastLockoutNoticeMs.load(std::memory_order_relaxed);
        if (lastNotice == 0U || nowMs - lastNotice >= 60000UL) {
            ESP_LOGW(TAG,
                     "Web auth POST refused: lockout active (~%lu s remaining)",
                     static_cast<unsigned long>(remSec));
            s_lastLockoutNoticeMs.store(nowMs, std::memory_order_relaxed);
        }
        streamAuthPage(req, false, remSec);
        return;
    }

    if (!webAuthValidateCsrfPost(req)) {
        webRedirect(req, F("/auth?bad=1"));
        return;
    }
    if (!req->hasParam("code", true)) {
        webRedirect(req, F("/auth?bad=1"));
        return;
    }

    const AsyncWebParameter* p       = req->getParam("code", true);
    const String&            codeStr = (p != nullptr) ? p->value() : String();
    if (!authCodeSyntaxOk(codeStr.c_str())) {
        webRedirect(req, F("/auth?bad=1"));
        return;
    }
    const uint32_t entered = static_cast<uint32_t>(strtoul(codeStr.c_str(), nullptr, 10));

    if (!tryConsumeAuthChallenge(entered, nowMs)) {
        const unsigned fails =
            s_authFailStreak.fetch_add(1, std::memory_order_acq_rel) + 1U;
        ESP_LOGD(TAG, "Web auth POST: invalid challenge code (%u/%u failures)", fails,
                 static_cast<unsigned>(kAuthFailsForLock));
        if (fails >= kAuthFailsForLock) {
            s_authLockoutStartMs.store(nowMs, std::memory_order_release);
            s_authFailStreak.store(0, std::memory_order_release);
            ESP_LOGW(TAG, "Web auth lockout activated after repeated failures (~%lu h)",
                     static_cast<unsigned long>(kAuthLockoutMs / 3600000UL));
        }
        streamAuthPage(req, true);
        return;
    }

    s_authFailStreak.store(0, std::memory_order_relaxed);

    portENTER_CRITICAL(&s_authMux);
    esp_fill_random(s_sessionRaw, sizeof(s_sessionRaw));
    s_sessionActive     = true;
    s_sessionCreatedMs  = nowMs;
    rotateCsrfTokenLocked();
    portEXIT_CRITICAL(&s_authMux);

    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();

    char nextPath[256] = "/";
    if (req->hasParam("next", true)) {
        const AsyncWebParameter* np = req->getParam("next", true);
        if (np != nullptr && isSafeNextPath(np->value().c_str())) {
            strlcpy(nextPath, np->value().c_str(), sizeof(nextPath));
        }
    }

    char hexCookie[33];
    portENTER_CRITICAL(&s_authMux);
    hexEncode16(s_sessionRaw, hexCookie);
    portEXIT_CRITICAL(&s_authMux);

    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), nextPath);
    char cookieBuf[160];
    snprintf(cookieBuf, sizeof(cookieBuf), "%s=%s; Path=/; HttpOnly; Max-Age=%lu; SameSite=Strict",
             kCookieName, hexCookie, static_cast<unsigned long>(kSessionCookieMaxAgeSec));
    resp->addHeader(F("Set-Cookie"), cookieBuf);
    ESP_LOGI(TAG, "Web auth session established (login)");
    webAddSecurityHeaders(resp);
    req->send(resp);
}

static void handleLogoutPost(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        webRedirect(req, F("/"));
        return;
    }
    if (configIsApMode()) {
        webRedirect(req, F("/"));
        return;
    }
    if (!webAuthIsAuthenticated(req)) {
        webRedirect(req, F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        webRedirect(req, F("/"));
        return;
    }

    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();

    webAuthInvalidateSession();
    ESP_LOGI(TAG, "Web auth session cleared (logout)");
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), String("/auth"));
    resp->addHeader(
        F("Set-Cookie"),
        F("chaya_sid=; Path=/; HttpOnly; Max-Age=0; SameSite=Strict"));
    webAddSecurityHeaders(resp);
    req->send(resp);
}

void webAuthInvalidateSession() {
    portENTER_CRITICAL(&s_authMux);
    s_sessionActive    = false;
    s_sessionCreatedMs = 0;
    memset(s_sessionRaw, 0, sizeof(s_sessionRaw));
    portEXIT_CRITICAL(&s_authMux);
}

void webAuthRegisterRoutes(AsyncWebServer& ws) {
    ws.on("/auth", HTTP_GET, [](AsyncWebServerRequest* rq) { handleAuthGet(rq); });
    ws.on("/auth", HTTP_POST, [](AsyncWebServerRequest* rq) { handleAuthPost(rq); });
    ws.on("/logout", HTTP_POST, [](AsyncWebServerRequest* rq) { handleLogoutPost(rq); });
}
