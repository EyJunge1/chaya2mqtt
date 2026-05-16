#include "wlan.h"

#include "constants.h"
#include "counter.h"
#include "pins.h"
#include "web/admin.h"
#include "web/auth.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiType.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "nvs_utils.h"
#include <cstring>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <time.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "WIFI";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

static constexpr char kDeviceHostname[] = "chaya2mqtt";
static constexpr char kSetupApSsid[]    = "Chaya2MQTT";

/** SSID NVS pointed at when STA join failed during boot (AP fallback); cleared after successful STA boot. */
static char g_lastFailedBootSsid[kWifiSsidMaxLen]{};

static DNSServer    g_dnsServer;
static bool         g_apMode = false;

static unsigned long     s_wifiReconnectNextAllowedMs = 0;
static uint32_t          s_wifiReconnectFailCount     = 0;
static std::atomic<bool> s_mdnsRestartNeeded{false};

/** While false, DISCONNECT events do not call WiFi.reconnect() (setup does its own wait). */
static bool s_wifiSetupComplete = false;

/** Last millis() when ARDUINO_EVENT_WIFI_STA_GOT_IP fired; 0 after disconnect. Used by MQTT stability guard. */
static unsigned long s_staLastGotIpWallMs = 0;

static constexpr unsigned long kStaStableAfterGotIpMs = 3000UL;

/** WiFi scan only from main task: cache for /wifi-scan JSON. */
static constexpr size_t      kMaxScanCache = 40;
static WlanScanRow           s_wifiScanCache[kMaxScanCache]{};
static size_t                s_wifiScanCacheCount = 0;
static std::atomic<bool>     s_wifiScanKick{false};
static std::atomic<bool>     s_wifiScanInProgress{false};
static std::atomic<bool>     s_wifiScanHasValidCache{false};
static portMUX_TYPE          s_wifiScanCacheMux = portMUX_INITIALIZER_UNLOCKED;

static void wifiStationEvent(arduino_event_id_t event);

bool wlanLastStaBootFailureSsidSnapshot(char* outSsid, size_t maxLen) {
    if (outSsid == nullptr || maxLen == 0U) {
        return false;
    }
    if (g_lastFailedBootSsid[0] == '\0') {
        outSsid[0] = '\0';
        return false;
    }
    strlcpy(outSsid, g_lastFailedBootSsid, maxLen);
    return true;
}

void wlanRequestWifiScanRefresh() {
    s_wifiScanKick.store(true, std::memory_order_release);
}

bool wlanWifiScanCacheReady() {
    return s_wifiScanHasValidCache.load(std::memory_order_acquire)
           && !s_wifiScanInProgress.load(std::memory_order_acquire);
}

size_t wlanWifiScanCopySnapshot(WlanScanRow* out, size_t maxRows) {
    if (out == nullptr || maxRows == 0U) {
        return 0;
    }
    portENTER_CRITICAL(&s_wifiScanCacheMux);
    const size_t n = std::min(maxRows, s_wifiScanCacheCount);
    for (size_t i = 0; i < n; ++i) {
        out[i] = s_wifiScanCache[i];
    }
    portEXIT_CRITICAL(&s_wifiScanCacheMux);
    return n;
}

static void wifiScanServiceOnMainTask() {
    if (s_wifiScanKick.exchange(false, std::memory_order_acq_rel)) {
        WiFi.scanDelete();
        s_wifiScanInProgress.store(true, std::memory_order_release);
        s_wifiScanHasValidCache.store(false, std::memory_order_release);
        WiFi.scanNetworks(true, false, false, 500, 0, nullptr, nullptr);
    }

    if (!s_wifiScanInProgress.load(std::memory_order_acquire)) {
        return;
    }

    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        return;
    }
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        s_wifiScanInProgress.store(false, std::memory_order_release);
        s_wifiScanHasValidCache.store(false, std::memory_order_release);
        /* Retry on next wlanLoop iteration. */
        wlanRequestWifiScanRefresh();
        return;
    }
    if (n < 0) {
        s_wifiScanInProgress.store(false, std::memory_order_release);
        return;
    }

    const size_t toStore = std::min(static_cast<size_t>(n), kMaxScanCache);
    portENTER_CRITICAL(&s_wifiScanCacheMux);
    s_wifiScanCacheCount = toStore;
    for (size_t i = 0; i < toStore; ++i) {
        strlcpy(s_wifiScanCache[i].ssid, WiFi.SSID(static_cast<uint8_t>(i)).c_str(),
                sizeof(s_wifiScanCache[i].ssid));
        s_wifiScanCache[i].rssi = WiFi.RSSI(static_cast<uint8_t>(i));
        s_wifiScanCache[i].open = (WiFi.encryptionType(static_cast<uint8_t>(i)) == WIFI_AUTH_OPEN);
    }
    portEXIT_CRITICAL(&s_wifiScanCacheMux);
    WiFi.scanDelete();
    s_wifiScanInProgress.store(false, std::memory_order_release);
    s_wifiScanHasValidCache.store(true, std::memory_order_release);
}

bool configSaveWiFiCredentials(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    if (!app_nvs::writeString("wifi", "ssid", ssid)) {
        ESP_LOGE(TAG, "NVS wifi: schreiben fehlgeschlagen (/wifi-connect)");
        return false;
    }
    if (!app_nvs::writeString("wifi", "pass", password != nullptr ? password : "")) {
        ESP_LOGE(TAG, "NVS wifi: schreiben fehlgeschlagen (/wifi-connect)");
        return false;
    }
    return true;
}

bool configIsApMode() {
    return g_apMode;
}

static void wifiStationEvent(arduino_event_id_t event) {
    if (g_apMode) {
        return;
    }
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            s_staLastGotIpWallMs = 0;
            if (!s_wifiSetupComplete) {
                break;
            }
            const unsigned long nowMs = millis();
            if (nowMs < s_wifiReconnectNextAllowedMs) {
                ESP_LOGD(TAG, "WLAN reconnect übersprungen (Backoff)");
                break;
            }
            ESP_LOGW(TAG, "WLAN getrennt, versuche Reconnect...");
            if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
                WiFi.reconnect();
                constexpr unsigned long kBaseBackoffMs = 3000UL;
                constexpr unsigned long kMaxBackoffMs  = 120000UL;
                const uint32_t          shift =
                    std::min(s_wifiReconnectFailCount, static_cast<uint32_t>(6));
                const unsigned long backoff =
                    std::min(kBaseBackoffMs * (1UL << shift), kMaxBackoffMs);
                s_wifiReconnectFailCount++;
                s_wifiReconnectNextAllowedMs = nowMs + backoff;
            }
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_staLastGotIpWallMs           = millis();
            s_wifiReconnectFailCount     = 0;
            s_wifiReconnectNextAllowedMs = 0;
            ESP_LOGI(TAG, "WLAN Sta-IP: %s", WiFi.localIP().toString().c_str());
            s_mdnsRestartNeeded.store(true, std::memory_order_release);
            break;
        default:
            break;
    }
}

void setupWiFi() {
    s_wifiSetupComplete = false;
    webAdminRegisterRoutes();

    char ssid[kWifiSsidMaxLen];
    char pass[kWifiPassMaxLen];
    ssid[0] = '\0';
    pass[0] = '\0';
    static_cast<void>(app_nvs::readString("wifi", "ssid", ssid, sizeof(ssid)));
    static_cast<void>(app_nvs::readString("wifi", "pass", pass, sizeof(pass)));

    bool staConnected = false;

    if (ssid[0] != '\0') {
        WiFi.setHostname(kDeviceHostname);
        WiFi.mode(WIFI_STA);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.begin(ssid, pass);
        /* Register early so GOT_IP / DISCONNECT are handled during setup waits. */
        WiFi.onEvent(wifiStationEvent);

        const unsigned long start = millis();
        while ((WiFi.status() != WL_CONNECTED || WiFi.localIP()[0] == 0)
               && millis() - start < 10000) {
            delay(100);
        }
        staConnected = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    }

    if (staConnected) {
        g_lastFailedBootSsid[0] = '\0';
        /* No modem sleep until MQTT session is up — avoids BEACON_TIMEOUT during TLS/handshake. */
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_set_max_tx_power(52);

        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
        if (!MDNS.begin(kDeviceHostname)) {
            ESP_LOGW(TAG, "mDNS.begin fehlgeschlagen");
        }
        MDNS.addService("http", "tcp", 80);
        /* Default inactive time (~6 s) triggers BEACON_TIMEOUT during long TLS on the main loop. */
        (void)esp_wifi_set_inactive_time(WIFI_IF_STA, 30);
        ESP_LOGI(TAG, "WLAN STA bereit (%s / %s)", kDeviceHostname, WiFi.localIP().toString().c_str());
    }

    if (!staConnected) {
        g_apMode = true;

        if (ssid[0] != '\0') {
            strlcpy(g_lastFailedBootSsid, ssid, sizeof(g_lastFailedBootSsid));
        }

        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.softAPConfig(IPAddress(4, 3, 2, 1), IPAddress(4, 3, 2, 1), IPAddress(255, 255, 255, 0));
        WiFi.setHostname(kDeviceHostname);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(kSetupApSsid);
        delay(50);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        delay(100);
        g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        g_dnsServer.start(53, "*", WiFi.softAPIP());
        ESP_LOGI(TAG, "WLAN AP: %s, IP %s", kSetupApSsid, WiFi.softAPIP().toString().c_str());
    }

    s_wifiSetupComplete = true;
    webAdminWebServer().begin();
}

void releaseGpioHoldBeforeRestart() {
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kSpiCs));
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kButtonLed));
}

void resetAllSettings() {
    ESP_LOGW(TAG, "Factory Reset: alle Einstellungen loeschen...");
    wlanAbortWifiConnectionTest();
    g_lastFailedBootSsid[0] = '\0';
    webAuthInvalidateSession();
    webAdminWebServer().end();
    if (!g_apMode) {
        MDNS.end();
    }
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_MODE_NULL);

    Preferences preferences;
    if (preferences.begin("wifi", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("mqtt", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("cfg", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("chaya", false)) {
        preferences.clear();
        preferences.end();
    }
    counterResetRamAfterFactoryClear();
    delay(500);
    releaseGpioHoldBeforeRestart();
    ESP.restart();
}

void wlanLoop() {
    wifiScanServiceOnMainTask();
    wifiConnectionTestServiceLoop();
    if (g_apMode) {
        g_dnsServer.processNextRequest();
    }
    if (s_mdnsRestartNeeded.exchange(false, std::memory_order_acq_rel) && !g_apMode) {
        MDNS.end();
        if (!MDNS.begin(kDeviceHostname)) {
            ESP_LOGW(TAG, "mDNS.begin nach GOT_IP fehlgeschlagen");
        }
        MDNS.addService("http", "tcp", 80);
    }
}

bool wlanStaConnectedOk() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
}

bool wlanStaStableForMqtt() {
    if (!wlanStaConnectedOk() || s_staLastGotIpWallMs == 0) {
        return false;
    }
    return (millis() - s_staLastGotIpWallMs) >= kStaStableAfterGotIpMs;
}

bool wlanNtpSynced() {
    /* mbedTLS needs plausible wall time for TLS certificate validity checks (same threshold as counter/OTA). */
    return ntpTimeLooksSynced(time(nullptr));
}

void wlanSetStaPowerSaveMqttActive(bool mqttSessionActive) {
    if (g_apMode) {
        return;
    }
    if (mqttSessionActive) {
        if (!wlanStaConnectedOk()) {
            return;
        }
        WiFi.setSleep(true);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    } else {
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
}
