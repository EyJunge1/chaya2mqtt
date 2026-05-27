#include "wlan.h"

#include "test.h"
#include "wlan_config.h"
#include "wlan_internal.h"

#include "async/task_handles.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "constants.h"
#include "heart/counter.h"
#include "hw/pins.h"
#include "ip_format.h"
#include "ota/ota.h"
#include "web/admin.h"
#include "web/admin_globals.h"
#include "web/auth.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <time.h>

#include "diag/task_watchdog.h"
#include "log_tag.h"

DEFINE_LOG_TAG("WIFI");

char         g_lastFailedBootSsid[kWifiSsidMaxLen]{};
portMUX_TYPE g_lastFailedBootSsidMux = portMUX_INITIALIZER_UNLOCKED;

DNSServer         g_dnsServer;
std::atomic<bool> g_apMode{false};

std::atomic<unsigned long> s_wifiReconnectNextAllowedMs{0};
std::atomic<uint32_t>      s_wifiReconnectFailCount{0};
std::atomic<bool>          s_mdnsRestartNeeded{false};

std::atomic<bool> s_wifiSetupComplete{false};
std::atomic<bool> s_bootStaConnectPending{false};
std::atomic<bool> s_bootWifiSettled{false};
std::atomic<bool> s_bootStaFinishDone{false};
char              s_bootAttemptSsid[kWifiSsidMaxLen]{};
unsigned long     s_bootStaConnectStartMs = 0;

std::atomic<unsigned long> s_staLastGotIpWallMs{0};

WlanScanRow       s_wifiScanCache[kWlanWifiScanCacheMaxRows]{};
WlanScanRow       s_wifiScanRowWork[kWlanWifiScanCacheMaxRows]{};
size_t            s_wifiScanCacheCount = 0;
std::atomic<bool> s_wifiScanKick{false};
std::atomic<bool> s_wifiScanInProgress{false};
std::atomic<bool> s_wifiScanHasValidCache{false};
portMUX_TYPE      s_wifiScanCacheMux = portMUX_INITIALIZER_UNLOCKED;

std::atomic<unsigned long> s_lastWifiScanKickMs{0};
std::atomic<unsigned long> s_wifiScanNextAllowedMs{0};

void wlanWifiApiLock() {
    if (g_wifiApiMutex != nullptr) {
        xSemaphoreTake(g_wifiApiMutex, portMAX_DELAY);
    }
}

void wlanWifiApiUnlock() {
    if (g_wifiApiMutex != nullptr) {
        xSemaphoreGive(g_wifiApiMutex);
    }
}

bool wlanWifiApiLockTimed(uint32_t timeoutMs) {
    if (g_wifiApiMutex == nullptr) {
        return true;
    }
    return xSemaphoreTake(g_wifiApiMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool wlanLastStaBootFailureSsidSnapshot(char* outSsid, size_t maxLen) {
    if (outSsid == nullptr || maxLen == 0U) {
        return false;
    }
    portENTER_CRITICAL(&g_lastFailedBootSsidMux);
    if (g_lastFailedBootSsid[0] == '\0') {
        portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
        outSsid[0] = '\0';
        return false;
    }
    strlcpy(outSsid, g_lastFailedBootSsid, maxLen);
    portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    return true;
}

bool wlanFillStaLinkSnapshot(bool* outConnected, char* ipStr, size_t ipLen, char* ssidBuf,
                             size_t ssidLen, int* outRssi) {
    if (outConnected == nullptr || ipStr == nullptr || ssidBuf == nullptr || outRssi == nullptr
        || ipLen == 0U || ssidLen == 0U) {
        return false;
    }
    if (!wlanWifiApiLockTimed(500U)) {
        return false;
    }
    const bool ok = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    *outConnected = ok;
    if (!ok) {
        ipStr[0]   = '\0';
        ssidBuf[0] = '\0';
        *outRssi   = 0;
        wlanWifiApiUnlock();
        return true;
    }
    formatIpv4ToBuf(WiFi.localIP(), ipStr, ipLen);
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strlcpy(ssidBuf, reinterpret_cast<const char*>(ap.ssid), ssidLen);
    } else {
        ssidBuf[0] = '\0';
    }
    *outRssi = static_cast<int>(WiFi.RSSI());
    wlanWifiApiUnlock();
    return true;
}

bool wlanReadStaLocalIpForCommit(char* outIp, size_t ipLen) {
    if (outIp == nullptr || ipLen == 0U) {
        return false;
    }
    wlanWifiApiLock();
    const bool ok = WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
    if (ok) {
        formatIpv4ToBuf(WiFi.localIP(), outIp, ipLen);
    } else {
        outIp[0] = '\0';
    }
    wlanWifiApiUnlock();
    return ok;
}

bool configIsApMode() {
    return g_apMode.load(std::memory_order_relaxed);
}

void releaseGpioHoldBeforeRestart() {
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kSpiCs));
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kButtonLed));
}

void resetAllSettings() {
    if (otaBlocksDestructiveAction()) {
        ESP_LOGW(TAG, "Factory reset refused: OTA in progress");
        return;
    }
    ESP_LOGW(TAG, "Factory reset: erasing all settings...");
    g_systemShutdownInProgress.store(true, std::memory_order_release);
    vTaskDelay(pdMS_TO_TICKS(100));
    counterSuspendNvsSavesForFactoryReset();
    wlanAbortWifiConnectionTest();
    portENTER_CRITICAL(&g_lastFailedBootSsidMux);
    g_lastFailedBootSsid[0] = '\0';
    portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    webAuthInvalidateSession();
    webAdminWebServer().end();
    if (g_apMode.load(std::memory_order_relaxed)) {
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

    static_cast<void>(app_nvs::clearNamespace(kNvsNsWifi));
    chayaTaskWatchdogReset();
    static_cast<void>(app_nvs::clearNamespace(kNvsNsMqtt));
    chayaTaskWatchdogReset();
    static_cast<void>(app_nvs::clearNamespace(kNvsNsCfg));
    chayaTaskWatchdogReset();
    static_cast<void>(app_nvs::clearNamespace(kNvsNsChaya));
    chayaTaskWatchdogReset();
    counterResetRamAfterFactoryClear();
    delay(500);
    releaseGpioHoldBeforeRestart();
    ESP.restart();
}

void wlanLoop() {
    wlanBootConnectServiceLoop();
    wifiScanServiceOnMainTask();
    wifiConnectionTestServiceLoop();
    if (g_apMode.load(std::memory_order_relaxed)) {
        static unsigned long s_lastApDnsPollMs = 0UL;
        const unsigned long  nowMs             = millis();
        const int            apClients         = WiFi.softAPgetStationNum();
        if (apClients > 0 || s_lastApDnsPollMs == 0UL
            || (nowMs - s_lastApDnsPollMs) >= kApDnsPollIntervalMs) {
            g_dnsServer.processNextRequest();
            s_lastApDnsPollMs = nowMs;
        }
    }
    if (s_mdnsRestartNeeded.exchange(false, std::memory_order_acq_rel)
        && !g_apMode.load(std::memory_order_relaxed)) {
        MDNS.end();
        if (!MDNS.begin(kDeviceHostname)) {
            ESP_LOGW(TAG, "mDNS.begin after GOT_IP failed");
        }
        MDNS.addService("http", "tcp", 80);
    }
}

bool wlanStaConnectedOk() {
    wlanWifiApiLock();
    const bool ok = WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
    wlanWifiApiUnlock();
    return ok;
}

bool wlanStaStableForMqtt() {
    if (!wlanStaConnectedOk()) {
        return false;
    }
    const unsigned long t = s_staLastGotIpWallMs.load(std::memory_order_relaxed);
    if (t == 0UL) {
        return false;
    }
    return (millis() - t) >= kStaStableAfterGotIpMs;
}

bool wlanNtpSynced() {
    return ntpTimeLooksSynced(time(nullptr));
}

void wlanSetStaPowerSaveMqttActive(bool mqttSessionActive) {
    if (g_apMode.load(std::memory_order_relaxed)) {
        return;
    }
    wlanWifiApiLock();
    if (mqttSessionActive) {
        if (WiFi.status() != WL_CONNECTED || WiFi.localIP()[0] == 0) {
            wlanWifiApiUnlock();
            return;
        }
        WiFi.setSleep(true);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    } else {
        WiFi.setSleep(true);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }
    wlanWifiApiUnlock();
}
