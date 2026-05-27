#include <Arduino.h>

#include "admin.h"

#include "web/deferred_reboot.h"
#include "admin_globals.h"
#include "routes/admin_routes.h"

#include "async/app_task.h"
#include "async/event_types.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "heart/counter.h"
#include "display/display.h"
#include "ota/ota.h"
#include "wifi/wlan.h"
#include "auth/auth.h"
#include "web_events.h"

#include <ESPAsyncWebServer.h>
#include <climits>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("ADMIN");

// Routes, deferred flags, SSE tick.

AsyncWebServer& webAdminWebServer() {
    static AsyncWebServer server(80);
    return server;
}

namespace {
bool g_webAdminRoutesRegistered = false;
uint32_t s_webAdminMqttApplyQueuedVersion = 0;
} // namespace

void webAdminRegisterRoutes() {
    if (g_webAdminRoutesRegistered) {
        return;
    }
    g_webAdminRoutesRegistered = true;

    webAuthInit();
    AsyncWebServer& ws = webAdminWebServer();
    webAuthRegisterRoutes(ws);

    adminRoutesRegisterWifi(ws);
    adminRoutesRegisterMqtt(ws);
    adminRoutesRegisterApplication(ws);
    webEventsRegister(ws);
}

void webAdminScheduleWifiConfiguredReboot() {
    deferredRebootAfterWifiSave();
}

void webAdminLoop() {
    webAuthLoop();
    webEventsTick();

    if (g_webAdminSettingsApplyPending.exchange(false, std::memory_order_acq_rel)
        && !g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        uint8_t daysApply;
        bool    authApply = false;
        portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
        daysApply = g_webAdminPendingResetDays;
        authApply = g_webAdminPendingAuthEnabled;
        portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
        bool settingsNvsOk = true;
        if (!configSetResetPeriodDays(daysApply)) {
            settingsNvsOk = false;
        }
        if (!configSetWebAuthEnabled(authApply)) {
            settingsNvsOk = false;
        }
        if (!settingsNvsOk) {
            g_webAdminSettingsNvsWriteFailed.store(true, std::memory_order_release);
        } else {
            g_webAdminSettingsNvsWriteFailed.store(false, std::memory_order_release);
        }
    }

    if (g_webAdminMqttApplyVersion.load(std::memory_order_acquire) > s_webAdminMqttApplyQueuedVersion
        && !g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        NetCmd cmd = NetCmd::MqttSettingsChanged;
        if (xQueueSend(g_netCmdQueue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
            s_webAdminMqttApplyQueuedVersion =
                g_webAdminMqttApplyVersion.load(std::memory_order_acquire);
        } else {
            ESP_LOGW(TAG, "netCmd queue full (MqttSettingsChanged)");
            appTaskNotify();
        }
    }

    const bool rebootReq =
        g_webAdminRebootRequested.load(std::memory_order_acquire);
    const bool wifiReconnectReq =
        g_webAdminWifiReconnectRequested.load(std::memory_order_acquire);
    if (rebootReq || wifiReconnectReq) {
        if (otaBlocksDestructiveAction()) {
            ESP_LOGW(TAG, "Reboot/reconnect deferred: OTA in progress");
            return;
        }
        g_webAdminRebootRequested.store(false, std::memory_order_release);
        g_webAdminWifiReconnectRequested.store(false, std::memory_order_release);
        flushHeartCounterIfDirty();
        flushHeartSentCounterIfDirty();
        delay(200);
        releaseGpioHoldBeforeRestart();
        ESP.restart();
    }
}
