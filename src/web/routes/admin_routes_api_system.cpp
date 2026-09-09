#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "async/event_types.h"
#include "async/system_lifecycle.h"
#include "async/system_shutdown_pure.h"
#include "async/task_handles.h"
#include "battery/battery.h"
#include "battery/battery_pure.h"
#include "mqtt/config.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/admin.h"
#include "web/admin_restart_pure.h"
#include <ESPAsyncWebServer.h>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void handleApiRebootPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (g_systemShutdownInProgress.load(std::memory_order_acquire) ||
        g_factoryResetQueued.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (webAdminRestartBlocked(otaBlocksDestructiveAction(), mqttCfgApplyPending(),
                               g_webAdminSettingsApplyPending.load(std::memory_order_acquire),
                               webAdminMqttApplyUnqueued(),
                               g_webAdminApplyInFlight.load(std::memory_order_acquire) > 0U)) {
        sendErr(req, 503, "busy");
        return;
    }
    ESP_LOGI(TAG, "API reboot requested");
    g_webAdminRebootRequested.store(true, std::memory_order_release);
    sendOk(req, 200, "rebooting");
}

void handleApiResetPost(AsyncWebServerRequest *req, NetCmd cmd, const char *message) {
    const bool shutdown = g_systemShutdownInProgress.load(std::memory_order_acquire);
    const bool factoryQueued = g_factoryResetQueued.load(std::memory_order_acquire);
    const bool restartBlocked =
        webAdminRestartBlocked(otaBlocksDestructiveAction(), mqttCfgApplyPending(),
                               g_webAdminSettingsApplyPending.load(std::memory_order_acquire), webAdminMqttApplyUnqueued(),
                               g_webAdminApplyInFlight.load(std::memory_order_acquire) > 0U);
    const bool rebootReq = g_webAdminRebootRequested.load(std::memory_order_acquire);
    const bool wifiReconnectReq = g_webAdminWifiReconnectRequested.load(std::memory_order_acquire);
    if (factoryResetHttpBlocked(shutdown, factoryQueued, restartBlocked, rebootReq, wifiReconnectReq)) {
        sendErr(req, 503, shutdown || factoryQueued ? "shutdown" : "busy");
        return;
    }
    if (batteryCriticalLow(batteryPercent())) {
        sendErr(req, 503, "battery");
        return;
    }
    bool expected = false;
    if (!g_factoryResetQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                      std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (!netCmdTrySend(cmd)) {
        g_factoryResetQueued.store(false, std::memory_order_release);
        sendErr(req, 503, g_netCmdQueue == nullptr ? "unavailable" : "queue_full");
        return;
    }
    ESP_LOGW(TAG, "API reset queued: %s", message != nullptr ? message : "?");
    sendOk(req, 202, message);
}

void adminRoutesRegisterApiSystem(AsyncWebServer &ws) {
    adminAddJsonPost(ws, "/api/reboot", handleApiRebootPost, ApiGuard::Sta);
    adminAddJsonPost(
        ws, "/api/factory-reset",
        [](AsyncWebServerRequest *rq, JsonVariant &json) {
            if (!adminJsonRequireObject(rq, json)) {
                return;
            }
            handleApiResetPost(rq, NetCmd::FactoryResetRequested, "factory_reset");
        },
        ApiGuard::Sta);
}
