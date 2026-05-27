#include "web_middleware.h"

#include "auth/auth.h"
#include "config/app_config.h"
#include "util/log_tag.h"
#include "web_utils.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <atomic>
#include <esp_log.h>

DEFINE_LOG_TAG("WEB");

namespace {
constexpr unsigned long kMwAuthRejectLogMinIntervalMs = 10000UL;

std::atomic<unsigned long> s_lastChaya401Ms{0};
std::atomic<unsigned long> s_lastChaya403Ms{0};

bool mwShouldLogThrottle(unsigned long nowMs, std::atomic<unsigned long>& lastLoggedMs) {
    const unsigned long prev = lastLoggedMs.load(std::memory_order_relaxed);
    if (prev != 0U && nowMs - prev < kMwAuthRejectLogMinIntervalMs) {
        return false;
    }
    lastLoggedMs.store(nowMs, std::memory_order_relaxed);
    return true;
}

bool mwRejectIfHostOrOriginBad(AsyncWebServerRequest* req) {
    if (!webRequestHostAllowed(req) || !webRequestOriginAllowed(req)) {
        webSendEmpty(req, 403);
        return true;
    }
    return false;
}
} // namespace

ArMiddlewareCallback mwRequireAllowedHost() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webRequestHostAllowed(req)) {
            webSendEmpty(req, 403);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwRequireStaMode() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (configIsApMode()) {
            webRedirect(req, F("/"));
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwRequireApMode() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!configIsApMode()) {
            webRedirect(req, F("/"));
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwRequireSessionRedirectGet() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webRequestHostAllowed(req)) {
            webSendEmpty(req, 403);
            return;
        }
        if (webAuthRedirectIfUnauthenticated(req)) {
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwPostSessionAndCsrfRedirect(const char* csrfRedirectPath) {
    return [csrfRedirectPath](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (mwRejectIfHostOrOriginBad(req)) {
            return;
        }
        if (!webAuthIsAuthenticated(req)) {
            webRedirect(req, F("/auth"));
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            webRedirect(req, csrfRedirectPath);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwPostChayaSendGuard() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        const unsigned long nowMs = millis();
        if (mwRejectIfHostOrOriginBad(req)) {
            return;
        }
        if (!webAuthIsAuthenticated(req)) {
            if (mwShouldLogThrottle(nowMs, s_lastChaya401Ms)) {
                ESP_LOGW(TAG, "/chaya JSON POST denied: no session (401)");
            }
            webSendJson(req, 401, "{\"ok\":false}");
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            if (mwShouldLogThrottle(nowMs, s_lastChaya403Ms)) {
                ESP_LOGW(TAG, "/chaya JSON POST denied: CSRF mismatch (403)");
            }
            webSendJson(req, 403, "{\"ok\":false}");
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwApPostCsrfRedirect(const char* redirectOnMismatch) {
    return [redirectOnMismatch](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (mwRejectIfHostOrOriginBad(req)) {
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            webRedirect(req, redirectOnMismatch);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwWifiConnectPostGuard() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (mwRejectIfHostOrOriginBad(req)) {
            return;
        }
        if (configIsApMode()) {
            if (!webAuthValidateCsrfPost(req)) {
                webRedirect(req, F("/wifi"));
                return;
            }
            next();
            return;
        }
        // STA: always require CSRF (hidden field from /wifi) so LAN attackers cannot forge POST without
        // reading a page first. Session is still optional when Web-Auth is disabled.
        if (!webAuthValidateCsrfPost(req)) {
            webRedirect(req, F("/wifi"));
            return;
        }
        if (configGetWebAuthEnabled()) {
            if (!webAuthIsAuthenticated(req)) {
                webRedirect(req, F("/auth"));
                return;
            }
        }
        next();
    };
}

ArMiddlewareCallback mwWifiInfoOrApOpenGet() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webRequestHostAllowed(req)) {
            webSendEmpty(req, 403);
            return;
        }
        if (configIsApMode()) {
            next();
            return;
        }
        if (webAuthRedirectIfUnauthenticated(req)) {
            return;
        }
        next();
    };
}
