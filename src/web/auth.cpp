#include "auth.h"

#include "button.h"
#include "counter.h"
#include "display.h"
#include "mqtt_config.h"
#include "pages.h"
#include "wlan.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/portmacro.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "AUTH";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

static constexpr unsigned long kChallengeTtlMs   = 300000UL; // 5 min
static constexpr unsigned long kConfirmWindowMs  = 10000UL; // wait for physical button
static constexpr unsigned long kAuthLockoutMs    = 3600000UL; // 1 hour after repeated failures
static constexpr unsigned kAuthFailsForLock      = 3;
static constexpr unsigned long kSessionCookieMaxAgeSec = 86400UL;
static constexpr const char    kCookieName[]     = "chaya_sid";

/** Protects CSRF + session (Async web task vs main webAuthLoop / button reads). */
static portMUX_TYPE s_authMux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_csrfToken = 0;

static bool    s_sessionActive = false;
static uint8_t s_sessionRaw[16]{};

/** Challenge fields — visible across tasks without holding s_authMux. */
static std::atomic<uint32_t>        s_challengeCode{0};
static std::atomic<unsigned long>   s_challengeExpiresMs{0};
static std::atomic<bool>            s_challengePending{false};

static std::atomic<bool>           s_awaitingButtonConfirm{false};
static std::atomic<unsigned long> s_confirmExpiresMs{0};

static std::atomic<unsigned>       s_authFailStreak{0};
static std::atomic<unsigned long>  s_authLockoutUntilMs{0};

static bool isPublicPath(const String& uri) {
    if (configIsApMode()) {
        if (uri == "/wifi" || uri.startsWith("/wifi-scan") || uri == "/wifi-status"
            || uri.startsWith("/wifi-connect")) {
            return true;
        }
        return uri == "/" || uri == "/favicon.ico";
    }
    if (uri == "/wifi" || uri.startsWith("/wifi-scan") || uri == "/wifi-status"
        || uri.startsWith("/wifi-connect") || uri.startsWith("/auth") || uri == "/logout") {
        return true;
    }
    return false;
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
    if (active) {
        memcpy(sessionCopy, s_sessionRaw, 16);
    }
    portEXIT_CRITICAL(&s_authMux);

    if (!active) {
        return false;
    }
    uint8_t cookieRaw[16];
    if (!parseCookieSession(req, cookieRaw)) {
        return false;
    }
    return memcmp(cookieRaw, sessionCopy, 16) == 0;
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
    return true;
}

static String urlEncodePathQuery(const String& s) {
    String out;
    out.reserve(s.length() * 3);
    for (size_t i = 0; i < s.length(); ++i) {
        const char ch = s[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
            || ch == '-' || ch == '_' || ch == '.' || ch == '/' || ch == '?' || ch == '=' || ch == '&') {
            out += ch;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(ch));
            out += buf;
        }
    }
    return out;
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
    s_challengeExpiresMs.store(0, std::memory_order_relaxed);
    s_challengeCode.store(0, std::memory_order_relaxed);
}

static void awaitingClearAtomic() {
    s_awaitingButtonConfirm.store(false, std::memory_order_release);
    s_confirmExpiresMs.store(0, std::memory_order_relaxed);
}

static void challengeBeginAtomic(uint32_t code, unsigned long expiresMs) {
    s_challengeCode.store(code, std::memory_order_relaxed);
    s_challengeExpiresMs.store(expiresMs, std::memory_order_release);
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
    ESP_LOGI(TAG, "Auth-Code abgelaufen");
    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();
}

/** If idle, starts 10 s window + prompt on display + LED blink. Skips restart if confirm or challenge already pending. */
static void maybeStartAwaitingButtonConfirm(bool resetFailStreakFromGet) {
    if (s_awaitingButtonConfirm.load(std::memory_order_acquire)
        || s_challengePending.load(std::memory_order_acquire)) {
        return;
    }

    const unsigned long deadlineMs = millis() + kConfirmWindowMs;
    s_confirmExpiresMs.store(deadlineMs, std::memory_order_release);
    s_awaitingButtonConfirm.store(true, std::memory_order_release);

    if (resetFailStreakFromGet) {
        s_authFailStreak.store(0, std::memory_order_relaxed);
    }
    requestDeferredDrawAuthPrompt();
    buttonRequestAuthBlinkOnFromAsync();
}

void webAuthInit() {
    portENTER_CRITICAL(&s_authMux);
    s_csrfToken = esp_random();
    if (s_csrfToken == 0) {
        s_csrfToken = 0xa5a5a5a5U;
    }
    portEXIT_CRITICAL(&s_authMux);
}

uint32_t webAuthGetCsrfToken() {
    portENTER_CRITICAL(&s_authMux);
    const uint32_t t = s_csrfToken;
    portEXIT_CRITICAL(&s_authMux);
    return t;
}

bool webAuthValidateCsrfPost(AsyncWebServerRequest* req) {
    if (!req->hasParam("csrf_token", true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam("csrf_token", true);
    if (p == nullptr) {
        return false;
    }
    const uint32_t tok = webAuthGetCsrfToken();
    char           tmp[24];
    snprintf(tmp, sizeof(tmp), "%lu", static_cast<unsigned long>(tok));
    return p->value() == tmp;
}

void webAuthHandleButtonDuringAuthBlink() {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        return;
    }
    if (!s_awaitingButtonConfirm.load(std::memory_order_acquire)) {
        return;
    }

    const unsigned long nowMs = millis();
    if (nowMs >= s_confirmExpiresMs.load(std::memory_order_acquire)) {
        return;
    }

    awaitingClearAtomic();

    const uint32_t      code      = esp_random() % 1000000U;
    const unsigned long expiresMs = millis() + kChallengeTtlMs;
    challengeBeginAtomic(code, expiresMs);
    requestDeferredDrawAuthCode(code);
}

void webAuthResetConfirmDeadline() {
    if (!s_awaitingButtonConfirm.load(std::memory_order_acquire)) {
        return;
    }
    s_confirmExpiresMs.store(millis() + kConfirmWindowMs, std::memory_order_release);
}

void webAuthLoop() {
    const unsigned long nowMs = millis();

    if (s_awaitingButtonConfirm.load(std::memory_order_acquire)) {
        if (nowMs >= s_confirmExpiresMs.load(std::memory_order_acquire)) {
            authConfirmWindowExpired();
            return;
        }
    }

    if (s_challengePending.load(std::memory_order_acquire)) {
        if (nowMs >= s_challengeExpiresMs.load(std::memory_order_acquire)) {
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
    const String nextEnc = urlEncodePathQuery(uri);
    req->redirect(String("/auth?next=") + nextEnc);
    return true;
}

static void handleAuthGet(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        req->redirect(F("/"));
        return;
    }
    bool wrong = false;
    if (req->hasParam("bad", false)) {
        wrong = true;
    }
    maybeStartAwaitingButtonConfirm(/*resetFailStreak=*/true);
    streamAuthPage(req, wrong);
}

/** Remaining seconds until lockout ends (rounded up); 0 if not locked out. */
static unsigned lockoutRemainingSec(unsigned long nowMs) {
    const unsigned long until = s_authLockoutUntilMs.load(std::memory_order_acquire);
    if (until == 0U || nowMs >= until) {
        return 0U;
    }
    const unsigned long diff = until - nowMs;
    const unsigned long sec  = (diff + 999UL) / 1000UL;
    return static_cast<unsigned>(
        std::min(sec, static_cast<unsigned long>(7200))); // sane upper bound if clock skew etc.
}

static void handleAuthPost(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        req->redirect(F("/"));
        return;
    }

    const unsigned long nowMs = millis();
    if (nowMs < s_authLockoutUntilMs.load(std::memory_order_acquire)) {
        streamAuthPage(req, false, lockoutRemainingSec(nowMs));
        return;
    }

    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/auth?bad=1"));
        return;
    }
    if (!req->hasParam("code", true)) {
        req->redirect(F("/auth?bad=1"));
        return;
    }

    const AsyncWebParameter* p       = req->getParam("code", true);
    const String&            codeStr = (p != nullptr) ? p->value() : String();
    const uint32_t           entered = static_cast<uint32_t>(strtoul(codeStr.c_str(), nullptr, 10));

    const bool     pending   = s_challengePending.load(std::memory_order_acquire);
    const bool     timeOk    = nowMs < s_challengeExpiresMs.load(std::memory_order_acquire);
    const uint32_t expected  = s_challengeCode.load(std::memory_order_relaxed);
    const bool     ok        = pending && timeOk && (entered == expected);

    if (!ok) {
        const unsigned fails =
            s_authFailStreak.fetch_add(1, std::memory_order_acq_rel) + 1U;
        if (fails >= kAuthFailsForLock) {
            s_authLockoutUntilMs.store(nowMs + kAuthLockoutMs, std::memory_order_release);
            s_authFailStreak.store(0, std::memory_order_release);
        }
        streamAuthPage(req, true);
        return;
    }

    s_authFailStreak.store(0, std::memory_order_relaxed);

    portENTER_CRITICAL(&s_authMux);
    esp_fill_random(s_sessionRaw, sizeof(s_sessionRaw));
    s_sessionActive = true;
    portEXIT_CRITICAL(&s_authMux);

    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();

    const char* next = "/";
    if (req->hasParam("next", true)) {
        const AsyncWebParameter* np = req->getParam("next", true);
        if (np != nullptr && isSafeNextPath(np->value().c_str())) {
            next = np->value().c_str();
        }
    }

    char hexCookie[33];
    portENTER_CRITICAL(&s_authMux);
    hexEncode16(s_sessionRaw, hexCookie);
    portEXIT_CRITICAL(&s_authMux);

    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), next);
    char cookieBuf[160];
    snprintf(cookieBuf, sizeof(cookieBuf), "%s=%s; Path=/; HttpOnly; Max-Age=%lu; SameSite=Lax",
             kCookieName, hexCookie, static_cast<unsigned long>(kSessionCookieMaxAgeSec));
    resp->addHeader(F("Set-Cookie"), cookieBuf);
    req->send(resp);
}

static void handleLogoutGet(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        req->redirect(F("/"));
        return;
    }
    if (configIsApMode()) {
        req->redirect(F("/"));
        return;
    }

    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();

    webAuthInvalidateSession();
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), String("/auth"));
    resp->addHeader(
        F("Set-Cookie"),
        F("chaya_sid=; Path=/; HttpOnly; Max-Age=0; SameSite=Lax"));
    req->send(resp);
}

void webAuthInvalidateSession() {
    portENTER_CRITICAL(&s_authMux);
    s_sessionActive = false;
    memset(s_sessionRaw, 0, sizeof(s_sessionRaw));
    portEXIT_CRITICAL(&s_authMux);
}

void webAuthRegisterRoutes(AsyncWebServer& ws) {
    ws.on("/auth", HTTP_GET, [](AsyncWebServerRequest* rq) { handleAuthGet(rq); });
    ws.on("/auth", HTTP_POST, [](AsyncWebServerRequest* rq) { handleAuthPost(rq); });
    ws.on("/logout", HTTP_GET, [](AsyncWebServerRequest* rq) { handleLogoutGet(rq); });
}
