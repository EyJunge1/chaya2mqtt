#include "wlan.h"

#include "counter.h"
#include "pins.h"
#include "web/admin.h"
#include "web/auth.h"

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
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

static DNSServer    g_dnsServer;
static bool         g_apMode = false;

static unsigned long     s_wifiReconnectNextAllowedMs = 0;
static uint32_t          s_wifiReconnectFailCount     = 0;
static std::atomic<bool> s_mdnsRestartNeeded{false};

/** WiFi scan only from main task: cache for /wifi-scan JSON. */
static constexpr size_t      kMaxScanCache = 40;
static WlanScanRow           s_wifiScanCache[kMaxScanCache]{};
static size_t                s_wifiScanCacheCount = 0;
static std::atomic<bool>     s_wifiScanKick{false};
static std::atomic<bool>     s_wifiScanInProgress{false};
static std::atomic<bool>     s_wifiScanHasValidCache{false};
static portMUX_TYPE          s_wifiScanCacheMux = portMUX_INITIALIZER_UNLOCKED;

static void wifiStationEvent(arduino_event_id_t event);

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
    Preferences preferences;
    if (!preferences.begin("wifi", false)) {
        ESP_LOGE(TAG, "NVS wifi: schreiben fehlgeschlagen (/wifi-connect)");
        return false;
    }
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password != nullptr ? password : "");
    preferences.end();
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
    webAdminRegisterRoutes();

    Preferences preferences;
    char        ssid[33];
    char        pass[65];
    ssid[0] = '\0';
    pass[0] = '\0';
    if (preferences.begin("wifi", true)) {
        preferences.getString("ssid", ssid, sizeof(ssid));
        preferences.getString("pass", pass, sizeof(pass));
        preferences.end();
    }

    bool staConnected = false;

    if (ssid[0] != '\0') {
        WiFi.mode(WIFI_STA);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname(kDeviceHostname);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.begin(ssid, pass);

        const unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            delay(100);
        }
        staConnected = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    }

    if (staConnected) {
        WiFi.setSleep(true);
        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
        esp_wifi_set_max_tx_power(52);
        if (!MDNS.begin(kDeviceHostname)) {
            ESP_LOGW(TAG, "mDNS.begin fehlgeschlagen");
        }
        MDNS.addService("http", "tcp", 80);
        WiFi.onEvent(wifiStationEvent);
        ESP_LOGI(TAG, "WLAN STA bereit (%s / %s)", kDeviceHostname, WiFi.localIP().toString().c_str());
    } else {
        g_apMode = true;

        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.softAPConfig(IPAddress(4, 3, 2, 1), IPAddress(4, 3, 2, 1), IPAddress(255, 255, 255, 0));
        WiFi.mode(WIFI_AP);
        WiFi.softAP(kSetupApSsid);
        delay(100);
        g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        g_dnsServer.start(53, "*", WiFi.softAPIP());
        ESP_LOGI(TAG, "WLAN AP: %s, IP %s", kSetupApSsid, WiFi.softAPIP().toString().c_str());
    }

    webAdminWebServer().begin();
}

void releaseGpioHoldBeforeRestart() {
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kSpiCs));
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kButtonLed));
}

void resetAllSettings() {
    ESP_LOGW(TAG, "Factory Reset: alle Einstellungen loeschen...");
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
