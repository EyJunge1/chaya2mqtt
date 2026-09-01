#include "web_middleware.h"

#include "csrf.h"
#include "util/log_tag.h"
#include "web_utils.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <atomic>
#include <esp_log.h>

DEFINE_LOG_TAG("WEB");

namespace {
constexpr unsigned long kMwCsrfRejectLogMinIntervalMs = 10000UL;

std::atomic<unsigned long> s_lastCsrf403Ms{0};

bool mwShouldLogThrottle(unsigned long nowMs, std::atomic<unsigned long> &lastLoggedMs) {
    const unsigned long prev = lastLoggedMs.load(std::memory_order_relaxed);
    if (prev != 0U && nowMs - prev < kMwCsrfRejectLogMinIntervalMs) {
        return false;
    }
    lastLoggedMs.store(nowMs, std::memory_order_relaxed);
    return true;
}

bool mwRejectIfHostOrOriginBad(AsyncWebServerRequest *req) {
    if (!webRequestHostAllowed(req) || !webRequestOriginAllowed(req)) {
        webSendEmpty(req, 403);
        return true;
    }
    return false;
}
} // namespace

ArMiddlewareCallback mwRequireAllowedHost() {
    return [](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        if (!webRequestHostAllowed(req)) {
            webSendEmpty(req, 403);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwApiPostCsrf() {
    return [](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        const unsigned long nowMs = millis();
        if (mwRejectIfHostOrOriginBad(req)) {
            return;
        }
        if (!webCsrfValidatePost(req)) {
            if (mwShouldLogThrottle(nowMs, s_lastCsrf403Ms)) {
                ESP_LOGW(TAG, "JSON POST denied: CSRF mismatch (403)");
            }
            webSendJsonError(req, 403, "csrf");
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwApiApPostCsrf() {
    return [](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        if (mwRejectIfHostOrOriginBad(req)) {
            return;
        }
        if (!configIsApMode()) {
            webSendJsonError(req, 400, "not_ap");
            return;
        }
        if (!webCsrfValidatePost(req)) {
            webSendJsonError(req, 403, "csrf");
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwApiStaMode() {
    return [](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        if (configIsApMode()) {
            webSendJsonError(req, 400, "ap_mode");
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwApiApMode() {
    return [](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        if (!webRequestHostAllowed(req)) {
            webSendEmpty(req, 403);
            return;
        }
        if (!configIsApMode()) {
            webSendJsonError(req, 400, "not_ap");
            return;
        }
        next();
    };
}
