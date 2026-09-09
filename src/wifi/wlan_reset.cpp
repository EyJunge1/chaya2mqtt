#include "wlan.h"

#include "test.h"
#include "wlan_config.h"
#include "wlan_internal.h"

#include "async/system_lifecycle.h"
#include "async/task_handles.h"
#include "async/web_server_hooks.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "constants.h"
#include "factory_wipe_pure.h"
#include "diag/task_watchdog.h"
#include "heart/counter.h"
#include "identity/device_identity.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/log_tag.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <nvs_flash.h>

DEFINE_LOG_TAG("WIFI");

static void prepareForResetAndRestart() {
    g_systemShutdownInProgress.store(true, std::memory_order_release);
    vTaskDelay(pdMS_TO_TICKS(100));
    wlanAbortWifiConnectionTest();
    portENTER_CRITICAL(&g_lastFailedBootSsidMux);
    g_lastFailedBootSsid[0] = '\0';
    portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    if (s_captiveDnsStarted.exchange(false, std::memory_order_acq_rel)) {
        g_dnsServer.stop();
    }
    if (!g_apMode.load(std::memory_order_relaxed)) {
        MDNS.end();
    }
    wlanWifiApiLock();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_MODE_NULL);
    wlanWifiApiUnlock();
}

WlanForceReassocResult wlanForceStaReassoc(const char *reasonTag) {
    if (g_apMode.load(std::memory_order_relaxed) || s_activeWlanConfig.ssid[0] == '\0') {
        return WlanForceReassocResult::SkippedConnected;
    }
    if (wlanEpdRefreshActive() || s_wifiScanInProgress.load(std::memory_order_acquire)) {
        ESP_LOGD(TAG, "WLAN force reassoc (%s) deferred (EPD or scan)", reasonTag != nullptr ? reasonTag : "n/a");
        return WlanForceReassocResult::Deferred;
    }
    // RC-NET-02: never disconnect+begin while OTA is running.
    if (otaBlocksDestructiveAction()) {
        ESP_LOGW(TAG, "WLAN force reassoc (%s) blocked by OTA — soft connect only", reasonTag != nullptr ? reasonTag : "n/a");
        wlanWifiApiLock();
        const wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_STA || mode == WIFI_AP_STA) {
            if (!WiFi.STA.connect()) {
                ESP_LOGD(TAG, "WiFi.STA.connect() failed");
            }
        }
        wlanWifiApiUnlock();
        // Soft connect is not a force — caller must not increment fail-count (RC-NET-10).
        return WlanForceReassocResult::Deferred;
    }
    // Hostname (NVS / g_nvsMutex) before g_wifiApiMutex — same order as wlan_boot.
    char staHostname[kDeviceStaHostnameBufLen]{};
    const bool haveStaHostname = buildDeviceStaHostname(staHostname, sizeof(staHostname));
    wlanWifiApiLock();
    if (wlanEpdRefreshActive() || s_wifiScanInProgress.load(std::memory_order_acquire)) {
        wlanWifiApiUnlock();
        ESP_LOGD(TAG, "WLAN force reassoc (%s) deferred (EPD or scan)", reasonTag != nullptr ? reasonTag : "n/a");
        return WlanForceReassocResult::Deferred;
    }
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
        wlanWifiApiUnlock();
        ESP_LOGD(TAG, "Stale WLAN force reassoc skipped: STA already connected");
        return WlanForceReassocResult::SkippedConnected;
    }
    wifi_ap_record_t ap{};
    [[maybe_unused]] const bool haveAp = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    ESP_LOGW(TAG,
             "WLAN force reassoc (%s) ssid='%s' reason=%u rssi=%d ch=%u heap free=%zu min=%zu "
             "largest=%zu",
             reasonTag != nullptr ? reasonTag : "n/a", s_activeWlanConfig.ssid,
             static_cast<unsigned>(s_lastStaDisconnectReason.load(std::memory_order_relaxed)),
             haveAp ? static_cast<int>(ap.rssi) : 0, haveAp ? static_cast<unsigned>(ap.primary) : 0U,
             static_cast<size_t>(esp_get_free_heap_size()), static_cast<size_t>(esp_get_minimum_free_heap_size()),
             static_cast<size_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));

    if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.disconnect(false);
        wlanWifiApiUnlock();
        delay(50);
        wlanWifiApiLock();
        // Scan/EPD may have started during the settle delay — abort force then.
        if (wlanEpdRefreshActive() || s_wifiScanInProgress.load(std::memory_order_acquire)) {
            wlanWifiApiUnlock();
            ESP_LOGD(TAG, "WLAN force reassoc (%s) deferred (EPD or scan)", reasonTag != nullptr ? reasonTag : "n/a");
            return WlanForceReassocResult::Deferred;
        }
        if (haveStaHostname) {
            WiFi.setHostname(staHostname);
        }
        if (!wlanApplyStaIpConfigLocked(s_activeWlanConfig)) {
            WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        }
        chayaTaskWatchdogUnsubscribe(TAG);
        WiFi.begin(s_activeWlanConfig.ssid, s_activeWlanConfig.pass);
        chayaTaskWatchdogSubscribe(TAG);
    }
    wlanWifiApiUnlock();
    return WlanForceReassocResult::Begun;
}

static void waitEpdIdleForDestructiveWork(const char *what) {
    const unsigned long waitStartMs = millis();
    while (wlanEpdRefreshActive()) {
        if ((millis() - waitStartMs) >= kWlanEpdWaitForDestructiveMs) {
            ESP_LOGW(TAG, "%s: EPD still active — continuing", what);
            break;
        }
        chayaTaskWatchdogReset();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool wlanControlledRestart(const char *reasonTag, void (*afterClaim)()) {
    if (g_factoryResetQueued.load(std::memory_order_acquire) || otaBlocksDestructiveAction() ||
        !systemShutdownTryClaim()) {
        ESP_LOGW(TAG, "WLAN controlled restart skipped (%s) — shutdown, factory, or OTA owns the device",
                 reasonTag != nullptr ? reasonTag : "n/a");
        return false;
    }
    if (afterClaim != nullptr) {
        afterClaim();
    }
    ESP_LOGE(TAG, "WLAN controlled restart (%s)", reasonTag != nullptr ? reasonTag : "n/a");
    flushAllHeartCountersIfDirty();
    waitEpdIdleForDestructiveWork("controlled restart");
    webServerEnd();
    prepareForResetAndRestart();
    delay(200);
    ESP.restart();
    return true;
}

static void factoryResetRamMirrors() {
    counterResetRamAfterFactoryClear();
    deviceIdentityResetRamAfterFactoryClear();
    otaResetRamAfterFactoryClear();
    wlanResetRamAfterFactoryClear();
    mqttCfgResetRamAfterFactoryClear();
}

void resetAllSettings() {
    // Exclusive claim so Soft-off / admin restart cannot latch-cut or ESP.restart() mid-wipe.
    if (!systemShutdownTryClaim()) {
        g_factoryResetQueued.store(false, std::memory_order_release);
        ESP_LOGW(TAG, "Factory reset refused: shutdown already in progress");
        return;
    }
    mqttAbortPendingPublish();
    webAdminClearRestartRequests();
    if (otaFlashInProgress() || otaBlocksDestructiveAction()) {
        g_factoryResetQueued.store(false, std::memory_order_release);
        systemShutdownRelease();
        ESP_LOGW(TAG, "Factory reset refused: OTA in progress");
        return;
    }
    ESP_LOGW(TAG, "Factory reset: erasing all settings...");
    counterSuspendNvsSavesForFactoryReset();
    waitEpdIdleForDestructiveWork("Factory reset");
    if (otaFlashInProgress() || otaBlocksDestructiveAction()) {
        counterResumeNvsSavesAfterFactoryResetAbort();
        g_factoryResetQueued.store(false, std::memory_order_release);
        systemShutdownRelease();
        ESP_LOGE(TAG, "Factory reset aborted: OTA in progress");
        return;
    }
    webServerEnd();
    static const char *const kFactoryNamespaces[] = {kNvsNsWifi, kNvsNsMqtt, kNvsNsCfg, kNvsNsChaya};
    app_nvs::NvsClearProgress progress{};
    bool erased = false;
    bool ready = false;
    {
        // RC-LIFE-04: hold g_nvsMutex across clear + fallback partition erase + re-init.
        app_nvs::ScopedNvsLock lock;
        progress = app_nvs::clearNamespacesUnlocked(kFactoryNamespaces,
                                                    sizeof(kFactoryNamespaces) / sizeof(kFactoryNamespaces[0]));
        chayaTaskWatchdogReset();
        ready = progress.allCleared;
        if (!progress.allCleared) {
            ESP_LOGE(TAG, "Factory reset namespace clear failed — erasing complete NVS partition");
            esp_err_t eraseErr = nvs_flash_erase();
            if (eraseErr != ESP_OK) {
                ESP_LOGE(TAG, "Factory reset NVS erase retry after %s", esp_err_to_name(eraseErr));
                eraseErr = nvs_flash_erase();
            }
            erased = eraseErr == ESP_OK;
            if (!erased) {
                ESP_LOGE(TAG, "Factory reset NVS erase failed: %s", esp_err_to_name(eraseErr));
            } else {
                esp_err_t initErr = nvs_flash_init();
                if (initErr != ESP_OK) {
                    ESP_LOGE(TAG, "Factory reset NVS init after erase retry: %s", esp_err_to_name(initErr));
                    initErr = nvs_flash_init();
                }
                ready = initErr == ESP_OK;
                if (!ready) {
                    ESP_LOGE(TAG, "Factory reset NVS init after erase failed: %s", esp_err_to_name(initErr));
                }
            }
            chayaTaskWatchdogReset();
        }
    }
    if (factoryWipeShouldAbort(progress.allCleared, progress.anyMutated, erased)) {
        counterResumeNvsSavesAfterFactoryResetAbort();
        g_factoryResetQueued.store(false, std::memory_order_release);
        systemShutdownRelease();
        webServerBegin();
        ESP_LOGE(TAG, "Factory reset aborted: NVS wipe failed — restore HTTP");
        return;
    }
    factoryResetRamMirrors();
    if (factoryWipeMustRestartUnready(ready, progress.anyMutated, erased)) {
        ESP_LOGE(TAG, "Factory reset: wipe not ready — restart without HTTP");
        ESP.restart();
        return;
    }
    // Tear down radio only after a successful wipe (BUG-LIFE-04).
    prepareForResetAndRestart();
    delay(500);
    ESP.restart();
}
