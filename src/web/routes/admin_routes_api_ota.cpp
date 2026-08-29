#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes_api_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "identity/device_identity.h"
#include "heart/counter.h"
#include "battery/battery.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/csrf.h"
#include "web/deferred_reboot.h"
#include "web/rate_limit.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/test.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

namespace {
WebMinIntervalLimit s_otaCheckLimit{5000U};
WebMinIntervalLimit s_otaInstallLimit{10000U};
} // namespace

void handleApiUpdateStatusGet(AsyncWebServerRequest* req) {
    adminSendJsonWithBuffer<384>(req, [](char* b, size_t n) {
        return otaFormatStatusJson(b, n) > 0U;
    });
}

bool parseOtaChannelParam(AsyncWebServerRequest* req, OtaChannel* out, bool* present) {
    if (out == nullptr || present == nullptr) {
        return false;
    }
    *present = false;
    if (!req->hasParam("channel", true)) {
        return true;
    }
    const AsyncWebParameter* p = req->getParam("channel", true);
    if (p == nullptr) {
        sendErr(req, 400, "channel");
        return false;
    }
    const String v = p->value();
    if (v == "stable") {
        *out     = OtaChannel::Stable;
        *present = true;
        return true;
    }
    if (v == "beta") {
        *out     = OtaChannel::Beta;
        *present = true;
        return true;
    }
    sendErr(req, 400, "channel");
    return false;
}

void handleApiUpdateCheckPost(AsyncWebServerRequest* req) {
    if (!webMinIntervalAllow(s_otaCheckLimit)) {
        sendErr(req, 429, "rate_limit");
        return;
    }
    if (otaBlocksDestructiveAction()) {
        sendErr(req, 503, "busy");
        return;
    }
    OtaChannel channel = otaGetChannel();
    bool       present = false;
    if (!parseOtaChannelParam(req, &channel, &present)) {
        return;
    }
    if (present) {
        if (!otaQueueGithubCheck(channel)) {
            sendErr(req, 500, "save");
            return;
        }
    } else {
        otaQueueGithubCheck();
    }
    ESP_LOGI(TAG, "API OTA check queued");
    sendOk(req, 200, "\"message\":\"checking\"");
}

void handleApiUpdateInstallPost(AsyncWebServerRequest* req) {
    if (!webMinIntervalAllow(s_otaInstallLimit)) {
        sendErr(req, 429, "rate_limit");
        return;
    }
    if (otaFlashInProgress()) {
        sendErr(req, 503, "busy");
        return;
    }
    OtaStatus st{};
    otaCopyStatus(&st);
    if (st.availableVersion[0] == '\0'
        || (st.phase != OtaPhase::Available && st.phase != OtaPhase::Error)) {
        sendErr(req, 409, "not_available");
        return;
    }
    ESP_LOGI(TAG, "API OTA install queued version=%s", st.availableVersion);
    otaQueueInstall();
    sendOk(req, 200, "\"message\":\"installing\"");
}


void adminRoutesRegisterApiOta(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h = ws.on("/api/update/status", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiUpdateStatusGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/update/check", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiUpdateCheckPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/update/install", HTTP_POST,
            [](AsyncWebServerRequest* rq) { handleApiUpdateInstallPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
