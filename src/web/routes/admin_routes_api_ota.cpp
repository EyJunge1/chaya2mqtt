#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "battery/battery.h"
#include "battery/battery_pure.h"
#include "ota/ota.h"
#include "ota/ota_json.h"
#include "util/log_tag.h"
#include "web/admin.h"
#include "web/web_utils.h"

#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

namespace {
bool otaHttpBlockedByApply(AsyncWebServerRequest *req) {
    if (webAdminOtaStartBlocked(mqttCfgApplyPending(), g_webAdminSettingsApplyPending.load(std::memory_order_acquire),
                                webAdminMqttApplyUnqueued(), g_webAdminApplyInFlight.load(std::memory_order_acquire) > 0U)) {
        sendErr(req, 503, "busy");
        return true;
    }
    return false;
}
} // namespace

void handleApiUpdateStatusGet(AsyncWebServerRequest *req) {
    JsonDocument doc;
    otaFillStatusJson(doc.to<JsonObject>());
    webSendJsonDoc(req, 200, doc);
}

void handleApiUpdateCheckPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (g_systemShutdownInProgress.load(std::memory_order_acquire) ||
        g_factoryResetQueued.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
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
    if (otaHttpBlockedByApply(req)) {
        return;
    }
    const ScopedWebAdminApplyInFlight applyInFlight;
    if (!applyInFlight || !applyInFlight.commitAllowed()) {
        sendErr(req, 503, "shutdown");
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
    otaQueueGithubCheck(channel);
    ESP_LOGI(TAG, "API OTA check queued");
    sendOk(req, 200, "checking");
}

void handleApiUpdateInstallPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (g_systemShutdownInProgress.load(std::memory_order_acquire) ||
        g_factoryResetQueued.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
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
    if (otaHttpBlockedByApply(req)) {
        return;
    }
    const ScopedWebAdminApplyInFlight applyInFlight;
    if (!applyInFlight || !applyInFlight.commitAllowed()) {
        sendErr(req, 503, "shutdown");
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
    adminOnGet(ws, "/api/update/status", handleApiUpdateStatusGet, ApiGuard::Sta);
    adminAddJsonPost(ws, "/api/update/check", handleApiUpdateCheckPost, ApiGuard::Sta);
    adminAddJsonPost(ws, "/api/update/install", handleApiUpdateInstallPost, ApiGuard::Sta);
}
