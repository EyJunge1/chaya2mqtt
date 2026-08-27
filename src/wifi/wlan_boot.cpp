#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"

#include "constants.h"
#include "device_identity.h"
#include "display/display.h"
#include "mqtt/config.h"
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

static void wlanFillStaHostname(char* out, size_t outLen) {
    if (!buildDeviceStaHostname(out, outLen)) {
        strlcpy(out, kDeviceHostname, outLen);
        ESP_LOGE(TAG, "Device ID unavailable; using non-unique STA hostname");
    }
}

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
    char staHostname[kDeviceStaHostnameBufLen]{};
    wlanFillStaHostname(staHostname, sizeof(staHostname));
    ESP_LOGI(TAG, "WLAN STA connecting to '%s' (%s)…", cfg.ssid,
             cfg.mode == WlanIpMode::Static ? "static" : "dhcp");
    wlanWifiApiLock();
    WiFi.setHostname(staHostname);
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
    // Start/restart mDNS from wlanLoop(), after re-validating that STA is still connected.
    s_mdnsRestartNeeded.store(true, std::memory_order_release);
    const esp_err_t inact = esp_wifi_set_inactive_time(WIFI_IF_STA, kWifiStaInactiveTimeSeconds);
    if (inact != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_inactive_time: %s", esp_err_to_name(inact));
    }
    {
        char ipStr[16];
        char staHostname[kDeviceStaHostnameBufLen]{};
        wlanFillStaHostname(staHostname, sizeof(staHostname));
        wlanWifiApiLock();
        formatIpv4ToBuf(WiFi.localIP(), ipStr, sizeof(ipStr));
        wlanWifiApiUnlock();
        ESP_LOGI(TAG, "WLAN STA ready (%s / %s)", staHostname, ipStr);
    }

    // The setup QR remains until STA connectivity is proven. Only then show
    // the waiting title or the operational heart.
    if (mqttCfgIsBrokerConfigured()) {
        requestDeferredDrawHeartScreen();
    } else {
        requestDeferredDrawSplashScreen();
    }
}

static int8_t            s_epdSavedTxPowerQdbm = 0;
static bool              s_epdTxPowerSaved     = false;

static bool wlanBringUpSetupSoftApLocked(const char* apPass, const char** outAuth) {
    WiFi.softAPConfig(IPAddress(4, 3, 2, 1), IPAddress(4, 3, 2, 1), IPAddress(255, 255, 255, 0));
    WiFi.setHostname(kDeviceHostname);
    WiFi.mode(WIFI_AP_STA);
    // WPA2/WPA3 transition matches camera WIFI QR (T:WPA); WPA3-only breaks many scanners.
    bool apOk = WiFi.softAP(kSetupApSsid, apPass, 1, 0, 4, false, WIFI_AUTH_WPA2_WPA3_PSK);
    const char* apAuth = "WPA2/WPA3";
    if (!apOk) {
        // Some stacks reject mixed mode; WPA2 still works with T:WPA QR payloads.
        apOk = WiFi.softAP(kSetupApSsid, apPass, 1, 0, 4, false, WIFI_AUTH_WPA2_PSK);
        apAuth = "WPA2";
    }
    if (outAuth != nullptr) {
        *outAuth = apAuth;
    }
    return apOk;
}

static bool wlanFinishSetupSoftAp(const char* apAuth) {
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
    wlanNoteCaptiveDnsStarted();
    wlanWifiApiUnlock();
    MDNS.end();
    if (!MDNS.begin(kDeviceHostname)) {
        ESP_LOGW(TAG, "mDNS.begin for setup AP failed");
    } else {
        MDNS.addService("http", "tcp", 80);
    }
    ESP_LOGI(TAG, "WLAN AP %s IP %s (%s setup AP)", kSetupApSsid, apIp, apAuth != nullptr ? apAuth : "?");
    return true;
}

void wlanBeginLowInterferenceForEpd() {
    if (WiFi.getMode() == WIFI_OFF || WiFi.getMode() == WIFI_MODE_NULL) {
        return;
    }
    if (!wlanWifiApiLockTimed(200U)) {
        return;
    }
    int8_t cur = 0;
    if (esp_wifi_get_max_tx_power(&cur) == ESP_OK) {
        s_epdSavedTxPowerQdbm = cur;
        s_epdTxPowerSaved     = true;
        // ~2 dBm: SoftAP stays up for local phones; EPD rail stops browning out.
        (void)esp_wifi_set_max_tx_power(8);
    }
    wlanWifiApiUnlock();
}

void wlanEndLowInterferenceForEpd() {
    if (!s_epdTxPowerSaved) {
        return;
    }
    if (!wlanWifiApiLockTimed(200U)) {
        return;
    }
    (void)esp_wifi_set_max_tx_power(s_epdSavedTxPowerQdbm);
    s_epdTxPowerSaved = false;
    wlanWifiApiUnlock();
}

void setupWifiStartApFallback(const char* attemptedSsid) {
    if (attemptedSsid[0] != '\0') {
        portENTER_CRITICAL(&g_lastFailedBootSsidMux);
        strlcpy(g_lastFailedBootSsid, attemptedSsid, sizeof(g_lastFailedBootSsid));
        portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    }

    if (!wlanEnsureSetupApPass()) {
        ESP_LOGE(TAG, "WiFi.softAP: setup PIN unavailable");
        return;
    }
    char apPass[kSetupApPassBufLen]{};
    if (!wlanApSetupPassSnapshot(apPass, sizeof(apPass))) {
        ESP_LOGE(TAG, "WiFi.softAP: setup PIN snapshot failed");
        return;
    }

    // Waveshare 08_E_paper_test paints with RF off. Cold-boot splash is drawn
    // in setup() before this; STA-fail fallback still needs a splash after SoftAP.
    const bool splashAlreadyDrawn = (attemptedSsid[0] == '\0');

    wlanWifiApiLock();
    WiFi.mode(WIFI_OFF);
    wlanWifiApiUnlock();
    delay(100);

    const char* apAuth = nullptr;
    wlanWifiApiLock();
    const bool apOk = wlanBringUpSetupSoftApLocked(apPass, &apAuth);
    wlanWifiApiUnlock();
    if (!apOk) {
        ESP_LOGE(TAG, "WiFi.softAP failed");
        return;
    }
    (void)wlanFinishSetupSoftAp(apAuth);
    if (!splashAlreadyDrawn) {
        requestDeferredDrawSplashScreen();
    }
}

void wlanHandleStaGotIpNetCmd() {
    if (!s_staGotIpWorkPending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (!wlanStaConnectedOk()) {
        ESP_LOGD(TAG, "Deferred GOT_IP ignored: STA no longer connected");
        return;
    }

    bool finishedBoot = false;
    if (s_bootStaConnectPending.exchange(false, std::memory_order_acq_rel)) {
        bool expectedFinish = false;
        if (s_bootStaFinishDone.compare_exchange_strong(expectedFinish, true,
                                                       std::memory_order_acq_rel)) {
            setupWifiFinishStaConnected();
            finishedBoot = true;
        }
        s_bootWifiSettled.store(true, std::memory_order_release);
        wlanNoteBootSettledNow();
    }

    if (!finishedBoot) {
        char ipStr[16]{};
        if (wlanWifiApiLockTimed(2000U)) {
            formatIpv4ToBuf(WiFi.localIP(), ipStr, sizeof(ipStr));
            wlanWifiApiUnlock();
            ESP_LOGI(TAG, "WLAN STA IP: %s", ipStr);
        } else {
            ESP_LOGW(TAG, "Deferred GOT_IP: WiFi API mutex timeout");
        }
        s_mdnsRestartNeeded.store(true, std::memory_order_release);
    }
}

void wlanBootConnectServiceLoop() {
    if (!s_bootStaConnectPending.load(std::memory_order_acquire)) {
        return;
    }
    wlanWifiApiLock();
    const bool connected = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    wlanWifiApiUnlock();
    const bool timedOut = millis() - s_bootStaConnectStartMs >= kWifiStaBootConnectTimeoutMs;
    switch (wlanBootDecide(true, connected, timedOut)) {
    case WlanBootAction::FinishSta: {
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
    case WlanBootAction::ContinueStaOnly:
        s_bootStaConnectPending.store(false, std::memory_order_release);
        ESP_LOGW(TAG,
                 "WLAN STA boot timeout for '%s' — setup AP remains disabled; continuing recovery",
                 s_bootAttemptSsid);
        s_bootWifiSettled.store(true, std::memory_order_release);
        wlanNoteBootSettledNow();
        return;
    case WlanBootAction::WaitForSta:
        return;
    case WlanBootAction::StartSetupAp:
        // This loop is only armed after loading stored STA credentials.
        return;
    }
}

void setupWiFi() {
    s_wifiSetupComplete.store(false, std::memory_order_release);
    s_bootWifiSettled.store(false, std::memory_order_release);
    s_bootStaConnectPending.store(false, std::memory_order_release);
    s_bootStaFinishDone.store(false, std::memory_order_release);
    s_staReconnectWorkPending.store(false, std::memory_order_release);
    s_staGotIpWorkPending.store(false, std::memory_order_release);
    webAdminRegisterRoutes();

    WlanConfig cfg{};
    const bool haveCfg = wlanLoadConfigFromNvs(&cfg);
    if (!haveCfg) {
        ESP_LOGD(TAG, "WLAN: no SSID in NVS (STA not configured or read failed)");
    }

    WiFi.onEvent(wifiStationEvent);

    const bool hasStaCredentials = haveCfg && cfg.ssid[0] != '\0';
    if (wlanBootDecide(hasStaCredentials, false, false) != WlanBootAction::StartSetupAp) {
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
