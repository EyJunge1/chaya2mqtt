#include "wlan.h"

#include "counter.h"
#include "web_admin.h"

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
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
/** GOT_IP (WiFi event task): restart mDNS in wifiLoop(). */
static std::atomic<bool> s_mdnsRestartNeeded{false};

static void wifiStationEvent(arduino_event_id_t event);

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
    String      ssid;
    String      pass;
    if (preferences.begin("wifi", true)) {
        ssid = preferences.getString("ssid", "");
        pass = preferences.getString("pass", "");
        preferences.end();
    }

    bool staConnected = false;

    if (ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname(kDeviceHostname);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.begin(ssid.c_str(), pass.c_str());

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
    (void)gpio_hold_dis(GPIO_NUM_15);
    (void)gpio_hold_dis(GPIO_NUM_4);
}

void resetAllSettings() {
    ESP_LOGW(TAG, "Factory Reset: alle Einstellungen loeschen...");
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

void wifiLoop() {
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
    webAdminLoop();
    if (!g_apMode) {
        maybePeriodicallyResetCounters();
    }
}

bool configIsSetupPortalActive() {
    return configIsApMode();
}

bool wlanStaConnectedOk() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
}
