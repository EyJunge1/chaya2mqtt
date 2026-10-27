#include <Arduino.h>

#include "admin.hpp"

#include "admin_globals.hpp"
#include "routes/admin_routes.hpp"
#include "web/admin_restart_pure.hpp"
#include "web/web_middleware.hpp"

#include "async/app_task.hpp"
#include "async/event_types.hpp"
#include "async/system_lifecycle.hpp"
#include "async/task_handles.hpp"
#include "async/web_server_hooks.hpp"
#include "audio/audio.hpp"
#include "config/app_config.hpp"
#include "config/nvs_utils.hpp"
#include "diag/task_watchdog.hpp"
#include "display/display.hpp"
#include "events.hpp"
#include "heart/counter.hpp"
#include "led/led.hpp"
#include "mqtt/config.hpp"
#include "mqtt/mqtt.hpp"
#include "ota/ota.hpp"
#include "wifi/wlan.hpp"

#include <ESPAsyncWebServer.h>
#include <atomic>
#include <climits>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.hpp"

DEFINE_LOG_TAG("ADMIN");

// Routes, deferred flags, SSE tick.

AsyncWebServer &webAdminWebServer() {
    static AsyncWebServer server(80);
    return server;
}

namespace {
bool g_webAdminRoutesRegistered = false;
std::atomic<uint32_t> s_webAdminMqttApplyQueuedVersion{0};
} // namespace

void webAdminRegisterRoutes() {
    if (g_webAdminRoutesRegistered) {
        return;
    }
    g_webAdminRoutesRegistered = true;

    AsyncWebServer &ws = webAdminWebServer();
    ws.addMiddleware(mwRequireAllowedHost());

    adminRoutesRegisterApi(ws);
    adminRoutesRegisterCaptive(ws);
    webEventsRegister(ws);
    adminRoutesRegisterSpa(ws); // SPA + onNotFound last
}

void webServerRegisterRoutes() { webAdminRegisterRoutes(); }

void webServerBegin() { webAdminWebServer().begin(); }

void webServerEnd() { webAdminWebServer().end(); }

void webAdminClearRestartRequests() {
    g_webAdminRebootRequested.store(false, std::memory_order_release);
    g_webAdminWifiReconnectRequested.store(false, std::memory_order_release);
}

bool webAdminMqttApplyUnqueued() {
    return webAdminMqttApplyUnqueuedPure(g_webAdminMqttApplyVersion.load(std::memory_order_acquire),
                                         s_webAdminMqttApplyQueuedVersion.load(std::memory_order_acquire));
}

void webAdminLoop() {
    webEventsTick();

    const bool shutdownInProgress = g_systemShutdownInProgress.load(std::memory_order_acquire);
    const bool otaBusyForApply = otaBlocksDestructiveAction();
    const bool factoryQueued = g_factoryResetQueued.load(std::memory_order_acquire);
    if (g_webAdminSettingsApplyPending.load(std::memory_order_acquire) &&
        !webAdminDeferredApplyAllowed(shutdownInProgress, otaBusyForApply, factoryQueued)) {
        if (!shutdownInProgress && !factoryQueued) {
            appTaskNotify();
        }
    } else if (g_webAdminSettingsApplyPending.load(std::memory_order_acquire) &&
               webAdminDeferredApplyAllowed(shutdownInProgress, otaBusyForApply, factoryQueued)) {
        uint8_t daysApply;
        char langApply[3];
        char themeApply[8];
        bool ledEnabledApply;
        bool einkDarkApply;
        bool speakerInstalledApply;
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
        uint32_t applyVersion;
        portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
        daysApply = g_webAdminPendingResetDays;
        strlcpy(langApply, g_webAdminPendingUiLang, sizeof(langApply));
        strlcpy(themeApply, g_webAdminPendingUiTheme, sizeof(themeApply));
        ledEnabledApply = g_webAdminPendingLedEnabled;
        einkDarkApply = g_webAdminPendingEinkDark;
        speakerInstalledApply = g_webAdminPendingSpeakerInstalled;
        audioTxEnabledApply = g_webAdminPendingAudioTxEnabled;
        audioRxEnabledApply = g_webAdminPendingAudioRxEnabled;
        audioTxVolumeApply = g_webAdminPendingAudioTxVolume;
        audioRxVolumeApply = g_webAdminPendingAudioRxVolume;
        quiet0Apply = g_webAdminPendingQuiet0;
        quiet1Apply = g_webAdminPendingQuiet1;
        txHzApply = g_webAdminPendingTxHz;
        txMsApply = g_webAdminPendingTxMs;
        rxHzApply = g_webAdminPendingRxHz;
        rxMsApply = g_webAdminPendingRxMs;
        applyVersion = g_webAdminSettingsApplyVersion.load(std::memory_order_acquire);
        portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
        // Attempt every write (no && short-circuit) so a mid-chain NVS fail does not
        // leave later fields unapplied (QUAL-04). Pending stays set until all succeed.
        // RC-WEB-42: timed write lock + TWDT between sets; abort remaining writes if Factory claimed.
        const app_nvs::ScopedNvsLockTimeout timedLock(app_nvs::kNvsSettingsApplyLockTimeoutTicks);
        bool ok = true;
        auto applyOne = [&](auto &&write) -> bool {
            chayaTaskWatchdogReset();
            if (g_systemShutdownInProgress.load(std::memory_order_acquire) ||
                g_factoryResetQueued.load(std::memory_order_acquire)) {
                return false;
            }
            return write();
        };
        ok &= applyOne([&] { return configSetResetPeriodDays(daysApply); });
        ok &= applyOne([&] { return configSetUiLang(langApply); });
        ok &= applyOne([&] { return configSetUiTheme(themeApply); });
        ok &= applyOne([&] { return configSetLedEnabled(ledEnabledApply); });
        ok &= applyOne([&] { return configSetEinkDark(einkDarkApply); });
        ok &= applyOne([&] { return configSetSpeakerInstalled(speakerInstalledApply); });
        ok &= applyOne([&] { return configSetAudioTxEnabled(audioTxEnabledApply); });
        ok &= applyOne([&] { return configSetAudioRxEnabled(audioRxEnabledApply); });
        ok &= applyOne([&] { return configSetAudioTxVolume(audioTxVolumeApply); });
        ok &= applyOne([&] { return configSetAudioRxVolume(audioRxVolumeApply); });
        ok &= applyOne([&] { return configSetAudioQuietHours(quiet0Apply, quiet1Apply); });
        ok &= applyOne([&] { return configSetAudioTones(txHzApply, txMsApply, rxHzApply, rxMsApply); });
        portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
        g_webAdminSettingsNvsWriteFailed.store(!ok, std::memory_order_release);
        if (ok) {
            if (g_webAdminSettingsApplyVersion.load(std::memory_order_acquire) == applyVersion) {
                g_webAdminSettingsApplyPending.store(false, std::memory_order_release);
                if (g_webAdminSettingsApplyVersion.load(std::memory_order_acquire) != applyVersion) {
                    g_webAdminSettingsApplyPending.store(true, std::memory_order_release);
                }
            }
        }
        portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
        if (ok) {
            ledApplyEnabled();
            if (speakerInstalledApply) {
                (void)audioEnsureCodecProbed();
            }
            (void)displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::Content, 0U);
        }
        // On failure keep pending so a later loop can retry (QUAL-04).
    }

    // RC-WEB-01: snapshot version before send; do not re-read after enqueue.
    const uint32_t v = g_webAdminMqttApplyVersion.load(std::memory_order_acquire);
    if (webAdminMqttApplyUnqueuedPure(v, s_webAdminMqttApplyQueuedVersion.load(std::memory_order_acquire)) &&
        webAdminDeferredApplyAllowed(g_systemShutdownInProgress.load(std::memory_order_acquire), otaBlocksDestructiveAction(),
                                     g_factoryResetQueued.load(std::memory_order_acquire))) {
        if (netCmdTrySend(NetCmd::MqttSettingsChanged, pdMS_TO_TICKS(500))) {
            s_webAdminMqttApplyQueuedVersion.store(v, std::memory_order_release);
        } else {
            ESP_LOGW(TAG, "netCmd queue full (MqttSettingsChanged)");
            appTaskNotify();
        }
    } else if (webAdminMqttApplyUnqueuedPure(v, s_webAdminMqttApplyQueuedVersion.load(std::memory_order_acquire)) &&
               !g_systemShutdownInProgress.load(std::memory_order_acquire) &&
               adminApplyBlockedByOta(otaBlocksDestructiveAction())) {
        appTaskNotify();
    }

    const bool rebootReq = g_webAdminRebootRequested.load(std::memory_order_acquire);
    const bool wifiReconnectReq = g_webAdminWifiReconnectRequested.load(std::memory_order_acquire);
    if (rebootReq || wifiReconnectReq) {
        // Factory / Soft-off / OTA own shutdown — never ESP.restart() and never clear their flag.
        if (g_systemShutdownInProgress.load(std::memory_order_acquire) || g_factoryResetQueued.load(std::memory_order_acquire)) {
            return;
        }
        const bool otaBusy = otaBlocksDestructiveAction();
        const bool mqttApplyPending = mqttCfgApplyPending();
        const bool settingsApplyPending = g_webAdminSettingsApplyPending.load(std::memory_order_acquire);
        const bool mqttApplyUnqueued = webAdminMqttApplyUnqueued();
        const bool applyInFlight = g_webAdminApplyInFlight.load(std::memory_order_acquire) > 0U;
        if (webAdminRestartBlocked(otaBusy, mqttApplyPending, settingsApplyPending, mqttApplyUnqueued, applyInFlight)) {
            if (otaBusy) {
                ESP_LOGW(TAG, "Reboot/reconnect deferred: OTA in progress");
            } else {
                ESP_LOGW(TAG, "Reboot/reconnect deferred: apply pending (mqtt=%d settings=%d unqueued=%d inflight=%d)",
                         mqttApplyPending ? 1 : 0, settingsApplyPending ? 1 : 0, mqttApplyUnqueued ? 1 : 0,
                         applyInFlight ? 1 : 0);
                appTaskNotify();
            }
            return;
        }
        // Raise shutdown before the second check so a MQTT POST cannot land
        // RAM-pending after we decided to restart (in-flight POST also blocks).
        if (!systemShutdownTryClaim()) {
            return;
        }
        const bool mqttApplyPending2 = mqttCfgApplyPending();
        const bool settingsApplyPending2 = g_webAdminSettingsApplyPending.load(std::memory_order_acquire);
        const bool mqttApplyUnqueued2 = webAdminMqttApplyUnqueued();
        const bool applyInFlight2 = g_webAdminApplyInFlight.load(std::memory_order_acquire) > 0U;
        if (webAdminRestartBlocked(otaBlocksDestructiveAction(), mqttApplyPending2, settingsApplyPending2, mqttApplyUnqueued2,
                                   applyInFlight2) ||
            g_factoryResetQueued.load(std::memory_order_acquire)) {
            systemShutdownRelease();
            ESP_LOGW(TAG, "Reboot/reconnect deferred: apply raced shutdown");
            appTaskNotify();
            return;
        }
        webServerEnd();
        ESP_LOGW(TAG, "Admin restart (reboot=%d wifiReconnect=%d)", rebootReq ? 1 : 0, wifiReconnectReq ? 1 : 0);
        g_webAdminRebootRequested.store(false, std::memory_order_release);
        g_webAdminWifiReconnectRequested.store(false, std::memory_order_release);
        mqttAbortPendingPublish();
        flushAllHeartCountersIfDirty();
        delay(200);
        flushAllHeartCountersIfDirty();
        ESP.restart();
    }
}
