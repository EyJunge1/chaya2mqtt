#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "battery/battery.h"
#include "battery/battery_pure.h"
#include "ota/ota.h"
#include "ota/ota_json.h"
#include "util/log_tag.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"

#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void handleApiUpdateStatusGet(AsyncWebServerRequest *req) {
    JsonDocument doc;
    otaFillStatusJson(doc.to<JsonObject>());
    webSendJsonDoc(req, 200, doc);
}

void handleApiUpdateCheckPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (batteryCriticalLow(batteryPercent())) {
        sendErr(req, 503, "battery_low");
        return;
    }
    if (otaBlocksDestructiveAction()) {
        sendErr(req, 503, "busy");
        return;
    }
    if (!adminJsonHasField(json, "channel")) {
        otaQueueGithubCheck();
        ESP_LOGI(TAG, "API OTA check queued");
        sendOk(req, 200, "checking");
        return;
    }
    char channelBuf[12]{};
    if (adminOptionalJsonString(json, "channel", channelBuf, sizeof(channelBuf)) != AdminJsonParam::Ok) {
        sendErr(req, 400, "channel");
        return;
    }
    OtaChannel channel = OtaChannel::Stable;
    if (strcmp(channelBuf, "stable") == 0) {
        channel = OtaChannel::Stable;
    } else if (strcmp(channelBuf, "beta") == 0) {
        channel = OtaChannel::Beta;
    } else {
        sendErr(req, 400, "channel");
        return;
    }
    if (!otaQueueGithubCheck(channel)) {
        sendErr(req, 500, "save");
        return;
    }
    ESP_LOGI(TAG, "API OTA check queued");
    sendOk(req, 200, "checking");
}

void handleApiUpdateInstallPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (batteryCriticalLow(batteryPercent())) {
        sendErr(req, 503, "battery_low");
        return;
    }
    if (otaFlashInProgress()) {
        sendErr(req, 503, "busy");
        return;
    }
    OtaStatus st{};
    otaCopyStatus(&st);
    if (st.availableVersion[0] == '\0' || (st.phase != OtaPhase::Available && st.phase != OtaPhase::Error)) {
        sendErr(req, 409, "not_available");
        return;
    }
    ESP_LOGI(TAG, "API OTA install queued version=%s", st.availableVersion);
    otaQueueInstall();
    sendOk(req, 200, "installing");
}

void adminRoutesRegisterApiOta(AsyncWebServer &ws) {
    {
        AsyncCallbackWebHandler &h =
            ws.on("/api/update/status", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiUpdateStatusGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackJsonWebHandler &h = adminAddJsonPost(ws, "/api/update/check", handleApiUpdateCheckPost);
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
    {
        AsyncCallbackJsonWebHandler &h = adminAddJsonPost(ws, "/api/update/install", handleApiUpdateInstallPost);
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
