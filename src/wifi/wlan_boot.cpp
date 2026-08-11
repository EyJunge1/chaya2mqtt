#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"

#include "constants.h"
#include "display/display.h"
#include "util/ip_format.h"
#include "web/admin.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstring>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <lwip/ip_addr.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

WlanConfig s_activeWlanConfig{};

static bool sntpSlotHasServer(uint8_t idx) {
    const char* name = esp_sntp_getservername(idx);
    if (name != nullptr && name[0] != '\0') {
        return true;
    }
    const ip_addr_t* addr = esp_sntp_getserver(idx);
    return addr != nullptr && !ip_addr_isany(addr);
}

void wlanEnableDhcpNtpRequest() {
#if LWIP_DHCP_GET_NTP_SRV
    esp_sntp_servermode_dhcp(true);
#endif
}

void wlanApplyNtpFromConfig(const WlanConfig& cfg) {
    if (cfg.ntp1[0] != '\0') {
        const char* ntp2 = cfg.ntp2[0] != '\0' ? cfg.ntp2 : nullptr;
        configTime(0, 0, cfg.ntp1, ntp2);
        ESP_LOGI(TAG, "NTP override: %s%s%s", cfg.ntp1, ntp2 != nullptr ? " / " : "",
                 ntp2 != nullptr ? ntp2 : "");
        return;
    }

    // Automatic: keep DHCP option-42 servers when present; else Cloudflare.
#if LWIP_DHCP_GET_NTP_SRV
    esp_sntp_servermode_dhcp(true);
#endif
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    }
    if (!sntpSlotHasServer(0)) {
        esp_sntp_setservername(0, kWifiDefaultNtp1);
        ESP_LOGI(TAG, "NTP automatic: no DHCP NTP — using %s", kWifiDefaultNtp1);
    } else if (!sntpSlotHasServer(1)) {
        esp_sntp_setservername(1, kWifiDefaultNtp1);
        ESP_LOGI(TAG, "NTP automatic: DHCP NTP present — %s as fallback", kWifiDefaultNtp1);
    } else {
        ESP_LOGI(TAG, "NTP automatic: DHCP NTP present");
    }
    if (!esp_sntp_enabled()) {
        esp_sntp_init();
    }
}

void setupWifiBeginStaConnectAsync(const WlanConfig& cfg) {
    s_activeWlanConfig = cfg;
    strlcpy(s_bootAttemptSsid, cfg.ssid, sizeof(s_bootAttemptSsid));
    ESP_LOGI(TAG, "WLAN STA connecting to '%s' (%s)…", cfg.ssid,
             cfg.mode == WlanIpMode::Static ? "static" : "dhcp");
    wlanWifiApiLock();
    WiFi.setHostname(kDeviceHostname);
    WiFi.mode(WIFI_STA);
    if (!wlanApplyStaIpConfigLocked(cfg)) {
        ESP_LOGW(TAG, "WiFi.config failed — falling back to DHCP");
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    // Must run before DHCP lease so option 42 NTP is accepted (automatic NTP only).
    if (cfg.ntp1[0] == '\0') {
        wlanEnableDhcpNtpRequest();
    }
    WiFi.begin(cfg.ssid, cfg.pass);
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

    wlanApplyNtpFromConfig(s_activeWlanConfig);
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
    const bool apOk = WiFi.softAP(kSetupApSsid);
    wlanWifiApiUnlock();
    if (!apOk) {
        ESP_LOGE(TAG, "WiFi.softAP failed");
        return;
    }
    g_apMode.store(true, std::memory_order_relaxed);
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
    ESP_LOGI(TAG, "WLAN AP %s IP %s (open setup AP)", kSetupApSsid, apIp);
    requestDeferredDrawSplashScreen();
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
        wlanNoteBootSettledNow();
        return;
    }
    if (millis() - s_bootStaConnectStartMs >= kWifiStaBootConnectTimeoutMs) {
        s_bootStaConnectPending.store(false, std::memory_order_release);
        setupWifiStartApFallback(s_bootAttemptSsid);
        s_bootWifiSettled.store(true, std::memory_order_release);
        wlanNoteBootSettledNow();
    }
}

void setupWiFi() {
    s_wifiSetupComplete.store(false, std::memory_order_release);
    s_bootWifiSettled.store(false, std::memory_order_release);
    s_bootStaConnectPending.store(false, std::memory_order_release);
    s_bootStaFinishDone.store(false, std::memory_order_release);
    webAdminRegisterRoutes();

    WlanConfig cfg{};
    const bool haveCfg = wlanLoadConfigFromNvs(&cfg);
    if (!haveCfg) {
        ESP_LOGD(TAG, "WLAN: no SSID in NVS (STA not configured or read failed)");
    }

    WiFi.onEvent(wifiStationEvent);

    if (haveCfg && cfg.ssid[0] != '\0') {
        setupWifiBeginStaConnectAsync(cfg);
    } else {
        setupWifiStartApFallback("");
        s_bootWifiSettled.store(true, std::memory_order_release);
        wlanNoteBootSettledNow();
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
