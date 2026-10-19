#include "web_middleware.hpp"

#include "web_utils.hpp"
#include "wifi/wlan.hpp"

#include <Arduino.h>

ArMiddlewareCallback mwRequireAllowedHost() {
    return [](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        if (!webRequestHostAllowed(req)) {
            webSendJsonError(req, 403, "host");
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
