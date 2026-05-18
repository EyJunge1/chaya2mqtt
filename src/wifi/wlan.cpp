#include "wlan.h"

#include "test.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "constants.h"
#include "ip_format.h"
#include "heart/counter.h"
#include "config/nvs_utils.h"
#include "hw/pins.h"
#include "web/admin.h"
#include "web/auth.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiType.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <time.h>

#include "log_tag.h"

DEFINE_LOG_TAG("WIFI");

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

// SSID that failed STA boot (AP mode); cleared on successful STA.
static char            g_lastFailedBootSsid[kWifiSsidMaxLen]{};
static portMUX_TYPE    g_lastFailedBootSsidMux = portMUX_INITIALIZER_UNLOCKED;

static DNSServer    g_dnsServer;
static std::atomic<bool> g_apMode{false};

static std::atomic<unsigned long> s_wifiReconnectNextAllowedMs{0};
static std::atomic<uint32_t>      s_wifiReconnectFailCount{0};
static std::atomic<bool>          s_mdnsRestartNeeded{false};

// After setupWiFi(): auto-reconnect on disconnect.
static std::atomic<bool> s_wifiSetupComplete{false};

// millis() at last GOT_IP; 0 if down (MQTT stability).
static std::atomic<unsigned long> s_staLastGotIpWallMs{0};

static constexpr unsigned long kStaStableAfterGotIpMs = 3000UL;

// Scan cache for /wifi-scan (filled in wlanLoop).
static WlanScanRow           s_wifiScanCache[kWlanWifiScanCacheMaxRows]{};
static wifi_ap_record_t      s_wifiScanApWorkRecords[kWlanWifiScanCacheMaxRows]{};
static WlanScanRow           s_wifiScanRowWork[kWlanWifiScanCacheMaxRows]{};
static size_t                s_wifiScanCacheCount = 0;
static std::atomic<bool>     s_wifiScanKick{false};
static std::atomic<bool>     s_wifiScanInProgress{false};
static std::atomic<bool>     s_wifiScanHasValidCache{false};
static portMUX_TYPE          s_wifiScanCacheMux = portMUX_INITIALIZER_UNLOCKED;

// Throttle rapid scan refresh (first kick always runs).
static std::atomic<unsigned long> s_lastWifiScanKickMs{0};
static constexpr unsigned long    kWifiScanKickMinIntervalMs = 20000UL;

namespace {

constexpr uint32_t kWifiCredPackedMagic = 0x43575631U;

struct PackedWifiCredentials {
    uint32_t magic;
    char     ssid[kWifiSsidMaxLen];
    char     pass[kWifiPassMaxLen];
};

static void wifiLoadCredentialsFromNvs(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
    if (ssid == nullptr || pass == nullptr || ssidLen == 0U || passLen == 0U) {
        return;
    }
    ssid[0] = '\0';
    pass[0] = '\0';
    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin("wifi", true)) {
        return;
    }

    bool loadedFromPacked = false;
    if (prefs.getBytesLength("cred_v1") == sizeof(PackedWifiCredentials)) {
        PackedWifiCredentials pk{};
        if (prefs.getBytes("cred_v1", &pk, sizeof(pk)) == sizeof(pk) && pk.magic == kWifiCredPackedMagic
            && pk.ssid[0] != '\0') {
            strlcpy(ssid, pk.ssid, ssidLen);
            strlcpy(pass, pk.pass, passLen);
            loadedFromPacked = true;
        }
    }
    if (!loadedFromPacked) {
        prefs.getString("ssid", ssid, ssidLen);
        prefs.getString("pass", pass, passLen);
    }
    prefs.end();

    if (ssid[0] != '\0') {
        ESP_LOGD(TAG, "WiFi NVS: credentials loaded (ssid=%s, packed=%s)", ssid,
                 loadedFromPacked ? "yes" : "no");
    } else {
        ESP_LOGD(TAG, "WiFi NVS: no SSID stored");
    }
}

} // namespace

static void wifiStationEvent(arduino_event_id_t event);

void wlanHandleStaReconnectNetCmd() {
    if (g_apMode.load(std::memory_order_relaxed)) {
        return;
    }
    const unsigned long    nowMs      = millis();
    const unsigned long    nextAllowed = s_wifiReconnectNextAllowedMs.load(std::memory_order_relaxed);
    if (nextAllowed != 0UL && static_cast<std::int32_t>(nowMs - nextAllowed) < 0) {
        ESP_LOGD(TAG, "WLAN reconnect skipped (backoff)");
        return;
    }
    ESP_LOGW(TAG, "WLAN disconnected, attempting reconnect...");
    wlanWifiApiLock();
    if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.reconnect();
        constexpr unsigned long kBaseBackoffMs = 3000UL;
        constexpr unsigned long kMaxBackoffMs  = 120000UL;
        const uint32_t          shift =
            std::min(s_wifiReconnectFailCount.load(std::memory_order_relaxed),
                     static_cast<uint32_t>(6));
        const unsigned long backoff =
            std::min(kBaseBackoffMs * (1UL << shift), kMaxBackoffMs);
        s_wifiReconnectFailCount.fetch_add(1, std::memory_order_relaxed);
        s_wifiReconnectNextAllowedMs.store(nowMs + backoff, std::memory_order_relaxed);
    }
    wlanWifiApiUnlock();
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

void wlanRequestWifiScanRefresh() {
    const unsigned long now = millis();
    const unsigned long last = s_lastWifiScanKickMs.load(std::memory_order_relaxed);
    if (last != 0UL && (now - last) < kWifiScanKickMinIntervalMs) {
        return;
    }
    s_lastWifiScanKickMs.store(now, std::memory_order_relaxed);
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

void wlanFillStaLinkSnapshot(bool* outConnected, char* ipStr, size_t ipLen, char* ssidBuf,
                             size_t ssidLen, int* outRssi) {
    if (outConnected == nullptr || ipStr == nullptr || ssidBuf == nullptr || outRssi == nullptr
        || ipLen == 0U || ssidLen == 0U) {
        return;
    }
    wlanWifiApiLock();
    const bool ok = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    *outConnected = ok;
    if (!ok) {
        ipStr[0]   = '\0';
        ssidBuf[0] = '\0';
        *outRssi   = 0;
        wlanWifiApiUnlock();
        return;
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

static void wifiScanServiceOnMainTask() {
    wlanWifiApiLock();
    if (s_wifiScanKick.exchange(false, std::memory_order_acq_rel)) {
        WiFi.scanDelete();
        s_wifiScanInProgress.store(true, std::memory_order_release);
        s_wifiScanHasValidCache.store(false, std::memory_order_release);
        WiFi.scanNetworks(true, false, false, 500, 0, nullptr, nullptr);
    }

    if (!s_wifiScanInProgress.load(std::memory_order_acquire)) {
        wlanWifiApiUnlock();
        return;
    }

    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        wlanWifiApiUnlock();
        return;
    }
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        s_wifiScanInProgress.store(false, std::memory_order_release);
        s_wifiScanHasValidCache.store(false, std::memory_order_release);
        s_wifiScanKick.store(true, std::memory_order_release);
        wlanWifiApiUnlock();
        return;
    }
    if (n < 0) {
        s_wifiScanInProgress.store(false, std::memory_order_release);
        wlanWifiApiUnlock();
        return;
    }

    const size_t toStore = std::min(static_cast<size_t>(n), kWlanWifiScanCacheMaxRows);
    uint16_t apFill = static_cast<uint16_t>(kWlanWifiScanCacheMaxRows);
    const esp_err_t  gr     = esp_wifi_scan_get_ap_records(&apFill, s_wifiScanApWorkRecords);
    if (gr != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(gr));
        WiFi.scanDelete();
        s_wifiScanInProgress.store(false, std::memory_order_release);
        s_wifiScanHasValidCache.store(false, std::memory_order_release);
        s_wifiScanKick.store(true, std::memory_order_release);
        wlanWifiApiUnlock();
        return;
    }
    const size_t usable = std::min(static_cast<size_t>(apFill), toStore);
    for (size_t i = 0; i < usable; ++i) {
        strlcpy(s_wifiScanRowWork[i].ssid,
                reinterpret_cast<const char*>(s_wifiScanApWorkRecords[i].ssid),
                sizeof(s_wifiScanRowWork[i].ssid));
        s_wifiScanRowWork[i].rssi = s_wifiScanApWorkRecords[i].rssi;
        s_wifiScanRowWork[i].open = (s_wifiScanApWorkRecords[i].authmode == WIFI_AUTH_OPEN);
    }
    portENTER_CRITICAL(&s_wifiScanCacheMux);
    s_wifiScanCacheCount = usable;
    for (size_t i = 0; i < usable; ++i) {
        s_wifiScanCache[i] = s_wifiScanRowWork[i];
    }
    portEXIT_CRITICAL(&s_wifiScanCacheMux);
    WiFi.scanDelete();
    s_wifiScanInProgress.store(false, std::memory_order_release);
    s_wifiScanHasValidCache.store(true, std::memory_order_release);
    wlanWifiApiUnlock();
}

bool configSaveWiFiCredentials(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    PackedWifiCredentials pk{};
    pk.magic = kWifiCredPackedMagic;
    strlcpy(pk.ssid, ssid, sizeof(pk.ssid));
    strlcpy(pk.pass, password != nullptr ? password : "", sizeof(pk.pass));

    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin("wifi", false)) {
        ESP_LOGE(TAG, "NVS wifi: begin(write) failed");
        return false;
    }
    prefs.remove("ssid");
    prefs.remove("pass");
    const size_t w = prefs.putBytes("cred_v1", &pk, sizeof(pk));
    prefs.end();
    if (w != sizeof(pk)) {
        ESP_LOGE(TAG, "NVS wifi: credential blob write failed");
        return false;
    }
    return true;
}

bool configIsApMode() {
    return g_apMode.load(std::memory_order_relaxed);
}

static void wifiStationEvent(arduino_event_id_t event) {
    if (g_apMode.load(std::memory_order_relaxed)) {
        return;
    }
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            s_staLastGotIpWallMs.store(0UL, std::memory_order_relaxed);
            if (!s_wifiSetupComplete.load(std::memory_order_acquire)) {
                break;
            }
            if (g_netCmdQueue != nullptr) {
                const NetCmd cmd = NetCmd::WifiReconnect;
                if (xQueueSend(g_netCmdQueue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
                    ESP_LOGW(TAG, "netCmd queue full (WifiReconnect)");
                }
            }
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
            s_staLastGotIpWallMs.store(millis(), std::memory_order_relaxed);
            s_wifiReconnectFailCount.store(0U, std::memory_order_relaxed);
            s_wifiReconnectNextAllowedMs.store(0UL, std::memory_order_relaxed);
            char ipStr[16];
            wlanWifiApiLock();
            formatIpv4ToBuf(WiFi.localIP(), ipStr, sizeof(ipStr));
            wlanWifiApiUnlock();
            ESP_LOGI(TAG, "WLAN STA IP: %s", ipStr);
            s_mdnsRestartNeeded.store(true, std::memory_order_release);
            break;
        }
        default:
            break;
    }
}

// Boot: try STA from NVS (blocks with event handler until timeout).
static bool setupWifiTryStaConnect(char* ssid, char* pass) {
    if (ssid[0] == '\0') {
        return false;
    }
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

    const unsigned long start = millis();
    for (;;) {
        wlanWifiApiLock();
        const bool connected = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
        wlanWifiApiUnlock();
        if (connected) {
            return true;
        }
        if (millis() - start >= kWifiStaBootConnectTimeoutMs) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// STA up: WiFi RF, mDNS, NTP.
static void setupWifiFinishStaConnected() {
    portENTER_CRITICAL(&g_lastFailedBootSsidMux);
    g_lastFailedBootSsid[0] = '\0';
    portEXIT_CRITICAL(&g_lastFailedBootSsidMux);
    wlanWifiApiLock();
    WiFi.setSleep(false);
    wlanWifiApiUnlock();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(kWifiStaMaxTxPowerQuarterDbm);

    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    if (!MDNS.begin(kDeviceHostname)) {
        ESP_LOGW(TAG, "mDNS.begin failed");
    }
    MDNS.addService("http", "tcp", 80);
    const esp_err_t inact =
        esp_wifi_set_inactive_time(WIFI_IF_STA, kWifiStaInactiveTimeSeconds);
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

// SoftAP + DNS hijack when STA missing or failed.
static void setupWifiStartApFallback(const char* attemptedSsid) {
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
    WiFi.softAP(kSetupApSsid);
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
    ESP_LOGI(TAG, "WLAN AP %s IP %s", kSetupApSsid, apIp);
}

void setupWiFi() {
    s_wifiSetupComplete.store(false, std::memory_order_release);
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

    const bool staConnected = setupWifiTryStaConnect(ssid, pass);

    if (staConnected) {
        setupWifiFinishStaConnected();
    } else {
        setupWifiStartApFallback(ssid);
    }

    s_wifiSetupComplete.store(true, std::memory_order_release);
    webAdminWebServer().begin();
}

void releaseGpioHoldBeforeRestart() {
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kSpiCs));
    (void)gpio_hold_dis(static_cast<gpio_num_t>(pins::kButtonLed));
}

void resetAllSettings() {
    ESP_LOGW(TAG, "Factory reset: erasing all settings...");
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

    static_cast<void>(app_nvs::clearNamespace("wifi"));
    static_cast<void>(app_nvs::clearNamespace("mqtt"));
    static_cast<void>(app_nvs::clearNamespace("cfg"));
    static_cast<void>(app_nvs::clearNamespace("chaya"));
    counterResetRamAfterFactoryClear();
    delay(500);
    releaseGpioHoldBeforeRestart();
    ESP.restart();
}

void wlanLoop() {
    wifiScanServiceOnMainTask();
    wifiConnectionTestServiceLoop();
    if (g_apMode.load(std::memory_order_relaxed)) {
        g_dnsServer.processNextRequest();
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
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
    wlanWifiApiUnlock();
}
