#include "web_middleware.h"

#include "auth.h"
#include "config/app_config.h"
#include "log_tag.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <esp_log.h>

DEFINE_LOG_TAG("WEB");

namespace {
constexpr unsigned long kMwAuthRejectLogMinIntervalMs = 10000UL;

bool mwShouldLogThrottle(unsigned long nowMs, unsigned long* lastLoggedMs) {
    if (*lastLoggedMs == 0U || nowMs - *lastLoggedMs >= kMwAuthRejectLogMinIntervalMs) {
        *lastLoggedMs = nowMs;
        return true;
    }
    return false;
}
} // namespace

ArMiddlewareCallback mwRequireStaMode() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (configIsApMode()) {
            req->redirect(F("/"));
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwRequireApMode() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!configIsApMode()) {
            req->redirect(F("/"));
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwRequireSessionRedirectGet() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (webAuthRedirectIfUnauthenticated(req)) {
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwPostSessionAndCsrfRedirect(const char* csrfRedirectPath) {
    return [csrfRedirectPath](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webAuthIsAuthenticated(req)) {
            req->redirect(F("/auth"));
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            req->redirect(csrfRedirectPath);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwPostChayaSendGuard() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        static unsigned long s_last401Ms = 0;
        static unsigned long s_last403Ms = 0;
        const unsigned long  nowMs      = millis();
        if (!webAuthIsAuthenticated(req)) {
            if (mwShouldLogThrottle(nowMs, &s_last401Ms)) {
                ESP_LOGW(TAG, "/chaya JSON POST denied: no session (401)");
            }
            req->send(401, "application/json", "{\"ok\":false}");
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            if (mwShouldLogThrottle(nowMs, &s_last403Ms)) {
                ESP_LOGW(TAG, "/chaya JSON POST denied: CSRF mismatch (403)");
            }
            req->send(403, "application/json", "{\"ok\":false}");
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwApPostCsrfRedirect(const char* redirectOnMismatch) {
    return [redirectOnMismatch](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webAuthValidateCsrfPost(req)) {
            req->redirect(redirectOnMismatch);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwWifiConnectPostGuard() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (configIsApMode()) {
            if (!webAuthValidateCsrfPost(req)) {
                req->redirect(F("/wifi"));
                return;
            }
            next();
            return;
        }
        // STA: always require CSRF (hidden field from /wifi) so LAN attackers cannot forge POST without
        // reading a page first. Session is still optional when Web-Auth is disabled.
        if (!webAuthValidateCsrfPost(req)) {
            req->redirect(F("/wifi"));
            return;
        }
        if (configGetWebAuthEnabled()) {
            if (!webAuthIsAuthenticated(req)) {
                req->redirect(F("/auth"));
                return;
            }
        }
        next();
    };
}

ArMiddlewareCallback mwWifiInfoOrApOpenGet() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
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
