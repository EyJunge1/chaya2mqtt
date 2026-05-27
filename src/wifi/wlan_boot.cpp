#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"

#include "constants.h"
#include "util/ip_format.h"
#include "mqtt/config.h"
#include "web/admin.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstring>
#include <esp_log.h>
#include <esp_wifi.h>
#include <time.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

void setupWifiBeginStaConnectAsync(const char* ssid, const char* pass) {
    strlcpy(s_bootAttemptSsid, ssid, sizeof(s_bootAttemptSsid));
    ESP_LOGI(TAG, "WLAN STA connecting to '%s'…", ssid);
    wlanWifiApiLock();
    WiFi.setHostname(kDeviceHostname);
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.begin(ssid, pass);
    wlanWifiApiUnlock();
    s_bootStaConnectStartMs = millis();
    s_bootStaConnectPending.store(true, std::memory_order_release);
}

void setupWifiFinishStaConnected() {
    portENTER_CRITICAL(&g_lastFailedBootSsidMux);
    g_lastFailedBootSsid[0] = '\0';
    portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    wlanWifiApiLock();
    WiFi.setSleep(true);
    wlanWifiApiUnlock();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_set_max_tx_power(kWifiStaMaxTxPowerQuarterDbm);

    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    if (!MDNS.begin(kDeviceHostname)) {
        ESP_LOGW(TAG, "mDNS.begin failed");
    }
    MDNS.addService("http", "tcp", 80);
    const esp_err_t inact = esp_wifi_set_inactive_time(WIFI_IF_STA, kWifiStaInactiveTimeSeconds);
    if (inact != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_inactive_time: %s", esp_err_to_name(inact));
    }
    {
        char ipStr[16];
        wlanWifiApiLock();
        formatIpv4ToBuf(WiFi.localIP(), ipStr, sizeof(ipStr));
        wlanWifiApiUnlock();
        ESP_LOGI(TAG, "WLAN STA ready (%s / %s)", kDeviceHostname, ipStr);
    }
}

void setupWifiStartApFallback(const char* attemptedSsid) {
    g_apMode.store(true, std::memory_order_relaxed);

    if (attemptedSsid[0] != '\0') {
        portENTER_CRITICAL(&g_lastFailedBootSsidMux);
        strlcpy(g_lastFailedBootSsid, attemptedSsid, sizeof(g_lastFailedBootSsid));
        portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    }

    wlanWifiApiLock();
    WiFi.mode(WIFI_OFF);
    wlanWifiApiUnlock();
    delay(100);
    wlanWifiApiLock();
    WiFi.softAPConfig(IPAddress(4, 3, 2, 1), IPAddress(4, 3, 2, 1), IPAddress(255, 255, 255, 0));
    WiFi.setHostname(kDeviceHostname);
    WiFi.mode(WIFI_AP_STA);
    char apPass[kWifiPassMaxLen]{};
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    if (deviceIdSyntaxOk(deviceId)) {
        static_cast<void>(snprintf(apPass, sizeof(apPass), "setup%s", deviceId));
    } else {
        strlcpy(apPass, "setupchaya", sizeof(apPass));
    }
    WiFi.softAP(kSetupApSsid, apPass);
    wlanWifiApiUnlock();
    delay(50);
    wlanWifiApiLock();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    wlanWifiApiUnlock();
    delay(100);
    char apIp[16]{};
    wlanWifiApiLock();
    formatIpv4ToBuf(WiFi.softAPIP(), apIp, sizeof(apIp));
    g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    g_dnsServer.start(53, "*", WiFi.softAPIP());
    wlanWifiApiUnlock();
    ESP_LOGI(TAG, "WLAN AP %s IP %s (PSK setup+<device-id>)", kSetupApSsid, apIp);
}

void wlanBootConnectServiceLoop() {
    if (!s_bootStaConnectPending.load(std::memory_order_acquire)) {
        return;
    }
    wlanWifiApiLock();
    const bool connected = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    wlanWifiApiUnlock();
    if (connected) {
        s_bootStaConnectPending.store(false, std::memory_order_release);
        bool expectedFinish = false;
        if (s_bootStaFinishDone.compare_exchange_strong(expectedFinish, true,
                                                       std::memory_order_acq_rel)) {
            setupWifiFinishStaConnected();
        }
        s_bootWifiSettled.store(true, std::memory_order_release);
        return;
    }
    if (millis() - s_bootStaConnectStartMs >= kWifiStaBootConnectTimeoutMs) {
        s_bootStaConnectPending.store(false, std::memory_order_release);
        setupWifiStartApFallback(s_bootAttemptSsid);
        s_bootWifiSettled.store(true, std::memory_order_release);
    }
}

void setupWiFi() {
    s_wifiSetupComplete.store(false, std::memory_order_release);
    s_bootWifiSettled.store(false, std::memory_order_release);
    s_bootStaConnectPending.store(false, std::memory_order_release);
    s_bootStaFinishDone.store(false, std::memory_order_release);
    webAdminRegisterRoutes();

    char ssid[kWifiSsidMaxLen];
    char pass[kWifiPassMaxLen];
    ssid[0] = '\0';
    pass[0] = '\0';
    wifiLoadCredentialsFromNvs(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] == '\0') {
        ESP_LOGD(TAG, "WLAN: no SSID in NVS (STA not configured or read failed)");
    }

    WiFi.onEvent(wifiStationEvent);

    if (ssid[0] != '\0') {
        setupWifiBeginStaConnectAsync(ssid, pass);
    } else {
        setupWifiStartApFallback("");
        s_bootWifiSettled.store(true, std::memory_order_release);
    }

    s_wifiSetupComplete.store(true, std::memory_order_release);
    webAdminWebServer().begin();
}

bool wlanIsSetupComplete() {
    return s_wifiSetupComplete.load(std::memory_order_acquire);
}

bool wlanIsBootWifiSettled() {
    return s_bootWifiSettled.load(std::memory_order_acquire);
}
