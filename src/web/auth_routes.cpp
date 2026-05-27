#include <Arduino.h>

#include "auth.h"

#include "auth_config.h"
#include "auth_internal.h"

#include "config/app_config.h"
#include "hw/button.h"
#include "pages.h"
#include "web_utils.h"
#include "wifi/wlan.h"

#include "util/auth_code_validation.h"
#include "util/log_tag.h"
#include "util/time_helpers.h"

#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>

DEFINE_LOG_TAG("AUTH");

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

static bool urlEncodePathQueryCStr(const char* s, char* out, size_t outLen) {
    if (s == nullptr || out == nullptr || outLen == 0U) {
        return false;
    }
    size_t pos = 0;
    for (; *s != '\0'; ++s) {
        const unsigned char uch = static_cast<unsigned char>(*s);
        char                chunk[5];
        const char*         piece;
        size_t              pieceLen;
        if ((uch >= 'a' && uch <= 'z') || (uch >= 'A' && uch <= 'Z') || (uch >= '0' && uch <= '9')
            || uch == '-' || uch == '_' || uch == '.' || uch == '/' || uch == '?' || uch == '='
            || uch == '&') {
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

static unsigned lockoutRemainingSec(unsigned long nowMs) {
    const unsigned long start = s_authLockoutStartMs.load(std::memory_order_acquire);
    if (start == 0UL || deadlineReached(start, kAuthLockoutMs, nowMs)) {
        return 0U;
    }
    const uint32_t remMs = remainingMs(start, kAuthLockoutMs, nowMs);
    const unsigned long sec = (static_cast<unsigned long>(remMs) + 999UL) / 1000UL;
    return static_cast<unsigned>(std::min(sec, static_cast<unsigned long>(7200)));
}

static std::atomic<unsigned long> s_lastLockoutNoticeMs{0};

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

static void handleAuthPost(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled()) {
        webRedirect(req, F("/"));
        return;
    }

    const unsigned long nowMs        = millis();
    const unsigned long lockoutStart = s_authLockoutStartMs.load(std::memory_order_acquire);
    if (lockoutStart != 0UL && !deadlineReached(lockoutStart, kAuthLockoutMs, nowMs)) {
        const unsigned long remSec = lockoutRemainingSec(nowMs);
        const unsigned long lastNotice =
            s_lastLockoutNoticeMs.load(std::memory_order_relaxed);
        if (lastNotice == 0U || nowMs - lastNotice >= 60000UL) {
            ESP_LOGW(TAG, "Web auth POST refused: lockout active (~%lu s remaining)",
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

    const AsyncWebParameter* p         = req->getParam("code", true);
    const String&            codeStr   = (p != nullptr) ? p->value() : String();
    if (!webAuthCodeSyntaxOk(codeStr.c_str())) {
        webRedirect(req, F("/auth?bad=1"));
        return;
    }
    const uint32_t entered = static_cast<uint32_t>(strtoul(codeStr.c_str(), nullptr, 10));

    if (!tryConsumeAuthChallenge(entered, nowMs)) {
        const unsigned fails = s_authFailStreak.fetch_add(1, std::memory_order_acq_rel) + 1U;
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
    s_sessionActive    = true;
    s_sessionCreatedMs = nowMs;
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
    resp->addHeader(F("Set-Cookie"), F("chaya_sid=; Path=/; HttpOnly; Max-Age=0; SameSite=Strict"));
    webAddSecurityHeaders(resp);
    req->send(resp);
}

void webAuthRegisterRoutes(AsyncWebServer& ws) {
    ws.on("/auth", HTTP_GET, [](AsyncWebServerRequest* rq) { handleAuthGet(rq); });
    ws.on("/auth", HTTP_POST, [](AsyncWebServerRequest* rq) { handleAuthPost(rq); });
    ws.on("/logout", HTTP_POST, [](AsyncWebServerRequest* rq) { handleLogoutPost(rq); });
}
