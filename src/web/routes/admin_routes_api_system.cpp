#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "battery/battery.h"
#include "battery/battery_pure.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/web_middleware.h"

#include <ESPAsyncWebServer.h>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void handleApiRebootPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    ESP_LOGI(TAG, "API reboot requested");
    g_webAdminRebootRequested.store(true, std::memory_order_release);
    sendOk(req, 200, "rebooting");
}

void handleApiResetPost(AsyncWebServerRequest *req, NetCmd cmd, const char *message) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (batteryCriticalLow(batteryPercent())) {
        sendErr(req, 503, "battery");
        return;
    }
    if (otaBlocksDestructiveAction()) {
        sendErr(req, 503, "busy");
        return;
    }
    if (!netCmdTrySend(cmd)) {
        sendErr(req, 503, g_netCmdQueue == nullptr ? "unavailable" : "queue_full");
        return;
    }
    ESP_LOGW(TAG, "API reset queued: %s", message != nullptr ? message : "?");
    sendOk(req, 202, message);
}

void adminRoutesRegisterApiSystem(AsyncWebServer &ws) {
    {
        AsyncCallbackJsonWebHandler &h = adminAddJsonPost(ws, "/api/reboot", handleApiRebootPost);
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
    {
        AsyncCallbackJsonWebHandler &h =
            adminAddJsonPost(ws, "/api/factory-reset", [](AsyncWebServerRequest *rq, JsonVariant &json) {
                if (!adminJsonRequireObject(rq, json)) {
                    return;
                }
                handleApiResetPost(rq, NetCmd::FactoryResetRequested, "factory_reset");
            });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
