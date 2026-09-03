#include "web_middleware.h"

#include "web_utils.h"
#include "wifi/wlan.h"

#include <Arduino.h>

ArMiddlewareCallback mwRequireAllowedHost() {
    return [](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        if (!webRequestHostAllowed(req)) {
            webSendEmpty(req, 403);
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
        if (!configIsApMode()) {
            webSendJsonError(req, 400, "not_ap");
            return;
        }
        next();
    };
}
