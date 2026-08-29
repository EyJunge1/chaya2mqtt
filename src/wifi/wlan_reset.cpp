#include "wlan.h"

#include "test.h"
#include "wlan_config.h"
#include "wlan_internal.h"

#include "async/task_handles.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "constants.h"
#include "diag/task_watchdog.h"
#include "heart/counter.h"
#include "hw/pins.h"
#include "identity/device_identity.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/admin.h"
#include "web/admin_globals.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstring>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <nvs_flash.h>

DEFINE_LOG_TAG("WIFI");

void releaseGpioHoldBeforeRestart() {
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kSpiCs));
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kDisplayPwrEn));
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kButtonLed));
}

static void prepareForResetAndRestart() {
    g_systemShutdownInProgress.store(true, std::memory_order_release);
    vTaskDelay(pdMS_TO_TICKS(100));
    wlanAbortWifiConnectionTest();
    portENTER_CRITICAL(&g_lastFailedBootSsidMux);
    g_lastFailedBootSsid[0] = '\0';
    portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    webAdminWebServer().end();
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

void wlanForceStaReassoc(const char* reasonTag) {
    if (g_apMode.load(std::memory_order_relaxed) || s_activeWlanConfig.ssid[0] == '\0') {
        return;
    }
    wlanWifiApiLock();
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
        wlanWifiApiUnlock();
        ESP_LOGD(TAG, "Stale WLAN force reassoc skipped: STA already connected");
        return;
    }
    wifi_ap_record_t ap{};
    [[maybe_unused]] const bool haveAp = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    ESP_LOGW(TAG,
             "WLAN force reassoc (%s) ssid='%s' reason=%u rssi=%d ch=%u heap free=%zu min=%zu "
             "largest=%zu",
             reasonTag != nullptr ? reasonTag : "n/a", s_activeWlanConfig.ssid,
             static_cast<unsigned>(s_lastStaDisconnectReason.load(std::memory_order_relaxed)),
             haveAp ? static_cast<int>(ap.rssi) : 0, haveAp ? static_cast<unsigned>(ap.primary) : 0U,
             static_cast<size_t>(esp_get_free_heap_size()),
             static_cast<size_t>(esp_get_minimum_free_heap_size()),
             static_cast<size_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));

    if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.disconnect(false);
        delay(50);
        char staHostname[kDeviceStaHostnameBufLen]{};
        if (buildDeviceStaHostname(staHostname, sizeof(staHostname))) {
            WiFi.setHostname(staHostname);
        }
        if (!wlanApplyStaIpConfigLocked(s_activeWlanConfig)) {
            WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        }
        WiFi.begin(s_activeWlanConfig.ssid, s_activeWlanConfig.pass);
    }
    wlanWifiApiUnlock();
}

void wlanControlledRestart(const char* reasonTag) {
    ESP_LOGE(TAG, "WLAN controlled restart (%s)", reasonTag != nullptr ? reasonTag : "n/a");
    flushAllHeartCountersIfDirty();
    prepareForResetAndRestart();
    delay(200);
    releaseGpioHoldBeforeRestart();
    ESP.restart();
}

void resetAllSettings() {
    if (otaBlocksDestructiveAction()) {
        ESP_LOGW(TAG, "Factory reset refused: OTA in progress");
        return;
    }
    ESP_LOGW(TAG, "Factory reset: erasing all settings...");
    counterSuspendNvsSavesForFactoryReset();
    prepareForResetAndRestart();

    bool cleared = app_nvs::clearNamespace(kNvsNsWifi);
    chayaTaskWatchdogReset();
    cleared = app_nvs::clearNamespace(kNvsNsMqtt) && cleared;
    chayaTaskWatchdogReset();
    cleared = app_nvs::clearNamespace(kNvsNsCfg) && cleared;
    chayaTaskWatchdogReset();
    cleared = app_nvs::clearNamespace(kNvsNsChaya) && cleared;
    chayaTaskWatchdogReset();
    if (!cleared) {
        ESP_LOGE(TAG, "Factory reset namespace clear failed — erasing complete NVS partition");
        const esp_err_t eraseErr = nvs_flash_erase();
        if (eraseErr != ESP_OK) {
            ESP_LOGE(TAG, "Factory reset NVS erase failed: %s", esp_err_to_name(eraseErr));
        }
        chayaTaskWatchdogReset();
    }
    counterResetRamAfterFactoryClear();
    delay(500);
    releaseGpioHoldBeforeRestart();
    ESP.restart();
}
