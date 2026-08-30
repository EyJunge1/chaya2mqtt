#include <Arduino.h>

#include "admin.h"

#include "web/deferred_reboot.h"
#include "admin_globals.h"
#include "routes/admin_routes.h"

#include "async/app_task.h"
#include "async/event_types.h"
#include "async/task_handles.h"
#include "async/web_server_hooks.h"
#include "config/app_config.h"
#include "heart/counter.h"
#include "led/led.h"
#include "ota/ota.h"
#include "wifi/wlan.h"
#include "csrf.h"
#include "events.h"

#include <ESPAsyncWebServer.h>
#include <climits>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("ADMIN");

// Routes, deferred flags, SSE tick.

namespace {
void webServerBeginImpl() {
    webAdminWebServer().begin();
}
void webServerEndImpl() {
    webAdminWebServer().end();
}
} // namespace

/** Register wifi↔web lifecycle hooks (QUAL-01). Call once before setupWiFi(). */
void webAdminInstallServerHooks() {
    webServerHooksRegister(webAdminRegisterRoutes, webServerBeginImpl, webServerEndImpl,
                           deferredRebootAfterWifiSave);
}

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

    webCsrfInit();
    AsyncWebServer& ws = webAdminWebServer();

    adminRoutesRegisterApi(ws);
    adminRoutesRegisterCaptive(ws);
    webEventsRegister(ws);
    adminRoutesRegisterSpa(ws); // SPA + onNotFound last
}

void webAdminScheduleWifiConfiguredReboot() {
    deferredRebootAfterWifiSave();
}

void webAdminLoop() {
    webEventsTick();

    if (g_webAdminSettingsApplyPending.load(std::memory_order_acquire)
        && !g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        uint8_t daysApply;
        char langApply[3];
        char themeApply[8];
        bool ledEnabledApply;
        bool audioTxEnabledApply;
        bool audioRxEnabledApply;
        uint8_t audioTxVolumeApply;
        uint8_t audioRxVolumeApply;
        uint8_t quiet0Apply;
        uint8_t quiet1Apply;
        uint16_t txHzApply;
        uint16_t txMsApply;
        uint16_t rxHzApply;
        uint16_t rxMsApply;
        portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
        daysApply = g_webAdminPendingResetDays;
        strlcpy(langApply, g_webAdminPendingUiLang, sizeof(langApply));
        strlcpy(themeApply, g_webAdminPendingUiTheme, sizeof(themeApply));
        ledEnabledApply     = g_webAdminPendingLedEnabled;
        audioTxEnabledApply = g_webAdminPendingAudioTxEnabled;
        audioRxEnabledApply = g_webAdminPendingAudioRxEnabled;
        audioTxVolumeApply  = g_webAdminPendingAudioTxVolume;
        audioRxVolumeApply  = g_webAdminPendingAudioRxVolume;
        quiet0Apply         = g_webAdminPendingQuiet0;
        quiet1Apply         = g_webAdminPendingQuiet1;
        txHzApply           = g_webAdminPendingTxHz;
        txMsApply           = g_webAdminPendingTxMs;
        rxHzApply           = g_webAdminPendingRxHz;
        rxMsApply           = g_webAdminPendingRxMs;
        portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
        // Attempt every write (no && short-circuit) so a mid-chain NVS fail does not
        // leave later fields unapplied (QUAL-04). Pending stays set until all succeed.
        bool ok = true;
        ok &= configSetResetPeriodDays(daysApply);
        ok &= configSetUiLang(langApply);
        ok &= configSetUiTheme(themeApply);
        ok &= configSetLedEnabled(ledEnabledApply);
        ok &= configSetAudioTxEnabled(audioTxEnabledApply);
        ok &= configSetAudioRxEnabled(audioRxEnabledApply);
        ok &= configSetAudioTxVolume(audioTxVolumeApply);
        ok &= configSetAudioRxVolume(audioRxVolumeApply);
        ok &= configSetAudioQuietHours(quiet0Apply, quiet1Apply);
        ok &= configSetAudioTones(txHzApply, txMsApply, rxHzApply, rxMsApply);
        g_webAdminSettingsNvsWriteFailed.store(!ok, std::memory_order_release);
        if (ok) {
            g_webAdminSettingsApplyPending.store(false, std::memory_order_release);
            ledApplyEnabled();
        }
        // On failure keep pending so a later loop can retry (QUAL-04).
    }

    if (g_webAdminMqttApplyVersion.load(std::memory_order_acquire) > s_webAdminMqttApplyQueuedVersion
        && !g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        if (netCmdTrySend(NetCmd::MqttSettingsChanged, pdMS_TO_TICKS(500))) {
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
        ESP_LOGW(TAG, "Admin restart (reboot=%d wifiReconnect=%d)", rebootReq ? 1 : 0,
                 wifiReconnectReq ? 1 : 0);
        g_webAdminRebootRequested.store(false, std::memory_order_release);
        g_webAdminWifiReconnectRequested.store(false, std::memory_order_release);
        flushAllHeartCountersIfDirty();
        delay(200);
        ESP.restart();
    }
}
