#include "web_auth.h"

#include "button.h"
#include "counter.h"
#include "display.h"
#include "mqtt_config.h"
#include "web_pages.h"
#include "wlan.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "AUTH";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

static constexpr unsigned long kChallengeTtlMs = 300000;  // 5 min
static constexpr const char    kCookieName[]   = "chaya_sid";

static uint32_t    s_csrfToken = 0;
static bool        s_sessionActive = false;
static uint8_t     s_sessionRaw[16]{};

static uint32_t    s_challengeCode    = 0;
static unsigned long s_challengeExpiresMs = 0;
static bool        s_challengePending   = false;

static bool isPublicPath(const String& uri) {
    if (configIsApMode()) {
        if (uri == "/wifi" || uri.startsWith("/wifi-scan")
            || uri.startsWith("/wifi-connect")) {
            return true;
        }
        return uri == "/" || uri == "/favicon.ico";
    }
    if (uri == "/wifi" || uri.startsWith("/wifi-scan") || uri.startsWith("/wifi-connect")
        || uri.startsWith("/auth")) {
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

static bool hexDecode32(const char* hex, uint8_t out16[16]) {
    if (strlen(hex) < 32) {
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
    const String& c = req->header("Cookie");
    const char*   key   = "chaya_sid=";
    const int     idx   = c.indexOf(key);
    if (idx < 0) {
        return false;
    }
    int start = idx + static_cast<int>(strlen(key));
    int end   = c.indexOf(';', start);
    if (end < 0) {
        end = c.length();
    }
    String tok = c.substring(start, end);
    tok.trim();
    if (tok.length() < 32) {
        return false;
    }
    return hexDecode32(tok.c_str(), outRaw);
}

static bool sessionMatchesRequest(AsyncWebServerRequest* req) {
    if (!s_sessionActive) {
        return false;
    }
    uint8_t cookieRaw[16];
    if (!parseCookieSession(req, cookieRaw)) {
        return false;
    }
    return memcmp(cookieRaw, s_sessionRaw, 16) == 0;
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

void webAuthInit() {
    s_csrfToken = esp_random();
    if (s_csrfToken == 0) {
        s_csrfToken = 0xa5a5a5a5U;
    }
}

uint32_t webAuthGetCsrfToken() {
    return s_csrfToken;
}

bool webAuthValidateCsrfPost(AsyncWebServerRequest* req) {
    if (!req->hasParam("csrf_token", true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam("csrf_token", true);
    if (p == nullptr) {
        return false;
    }
    char   tmp[24];
    snprintf(tmp, sizeof(tmp), "%lu", static_cast<unsigned long>(s_csrfToken));
    return p->value() == tmp;
}

void webAuthHandleButtonCancel() {
    if (!s_challengePending || !configGetWebAuthEnabled()) {
        return;
    }
    s_challengePending   = false;
    s_challengeExpiresMs = 0;
    s_challengeCode      = 0;
    buttonSetAuthBlinkActive(false);
    if (mqttCfg.server[0] != '\0' && !configIsApMode()) {
        drawHeartWithNumber();
    } else {
        drawSplashScreen();
    }
}

void webAuthLoop() {
    if (!s_challengePending) {
        return;
    }
    const unsigned long now = millis();
    if (now < s_challengeExpiresMs) {
        return;
    }
    ESP_LOGI(TAG, "Auth-Code abgelaufen");
    webAuthHandleButtonCancel();
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

static void beginNewChallenge() {
    s_challengeCode      = esp_random() % 1000000U;
    s_challengeExpiresMs = millis() + kChallengeTtlMs;
    s_challengePending   = true;
    drawAuthCode(s_challengeCode);
    buttonSetAuthBlinkActive(true);
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
    beginNewChallenge();
    streamAuthPage(req, wrong);
}

static void handleAuthPost(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        req->redirect(F("/"));
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
    const AsyncWebParameter* p = req->getParam("code", true);
    const String& codeStr = (p != nullptr) ? p->value() : String();
    const uint32_t entered = static_cast<uint32_t>(strtoul(codeStr.c_str(), nullptr, 10));
    const bool ok = s_challengePending && (entered == s_challengeCode) && (millis() < s_challengeExpiresMs);

    if (!ok) {
        beginNewChallenge();
        streamAuthPage(req, true);
        return;
    }

    esp_fill_random(s_sessionRaw, sizeof(s_sessionRaw));
    s_sessionActive      = true;
    s_challengePending   = false;
    s_challengeExpiresMs = 0;
    buttonSetAuthBlinkActive(false);
    if (mqttCfg.server[0] != '\0') {
        drawHeartWithNumber();
    } else {
        drawSplashScreen();
    }

    const char* next = "/";
    if (req->hasParam("next", true)) {
        const AsyncWebParameter* np = req->getParam("next", true);
        if (np != nullptr && isSafeNextPath(np->value().c_str())) {
            next = np->value().c_str();
        }
    }

    char hexCookie[33];
    hexEncode16(s_sessionRaw, hexCookie);

    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), next);
    const String cookie = String(kCookieName) + "=" + hexCookie + "; Path=/; HttpOnly; Max-Age=86400; SameSite=Lax";
    resp->addHeader(F("Set-Cookie"), cookie);
    req->send(resp);
}

void webAuthInvalidateSession() {
    s_sessionActive = false;
    memset(s_sessionRaw, 0, sizeof(s_sessionRaw));
}

void webAuthRegisterRoutes(AsyncWebServer& ws) {
    ws.on("/auth", HTTP_GET, [](AsyncWebServerRequest* rq) { handleAuthGet(rq); });
    ws.on("/auth", HTTP_POST, [](AsyncWebServerRequest* rq) { handleAuthPost(rq); });
}
