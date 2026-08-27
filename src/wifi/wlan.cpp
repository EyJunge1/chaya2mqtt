#include "wlan.h"

#include "test.h"
#include "wlan_config.h"
#include "wlan_internal.h"

#include "async/task_handles.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "constants.h"
#include "device_identity.h"
#include "heart/counter.h"
#include "hw/pins.h"
#include "util/ip_format.h"
#include "util/net_validate.h"
#include "ota/ota.h"
#include "web/admin.h"
#include "web/admin_globals.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <cstring>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <time.h>

#include "diag/task_watchdog.h"
#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

char         g_lastFailedBootSsid[kWifiSsidMaxLen]{};
portMUX_TYPE g_lastFailedBootSsidMux = portMUX_INITIALIZER_UNLOCKED;

DNSServer         g_dnsServer;
std::atomic<bool> g_apMode{false};
static std::atomic<bool> s_captiveDnsStarted{false};

std::atomic<unsigned long> s_wifiReconnectNextAllowedMs{0};
std::atomic<uint32_t>      s_wifiReconnectFailCount{0};
std::atomic<bool>          s_staReconnectWorkPending{false};
std::atomic<bool>          s_staGotIpWorkPending{false};
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

static char s_setupApPass[kSetupApPassBufLen]{};

bool wlanEnsureSetupApPass() {
    char stored[kSetupApPassBufLen]{};
    (void)app_nvs::readString(kNvsNsWifi, kNvsKeyWifiApPin, stored, sizeof(stored));
    if (setupApPassSyntaxOk(stored)) {
        strlcpy(s_setupApPass, stored, sizeof(s_setupApPass));
        return true;
    }

    uint32_t rnd = 0;
    esp_fill_random(&rnd, sizeof(rnd));
    if (!formatSetupApPassFromU32(rnd, s_setupApPass, sizeof(s_setupApPass))) {
        s_setupApPass[0] = '\0';
        return false;
    }
    if (!app_nvs::writeString(kNvsNsWifi, kNvsKeyWifiApPin, s_setupApPass)) {
        ESP_LOGW(TAG, "AP PIN NVS write failed; using RAM-only PIN");
    }
    return true;
}

bool wlanApSetupPassSnapshot(char* outPass, size_t passLen) {
    if (outPass == nullptr || passLen == 0U) {
        return false;
    }
    if (!setupApPassSyntaxOk(s_setupApPass)) {
        outPass[0] = '\0';
        return false;
    }
    strlcpy(outPass, s_setupApPass, passLen);
    return true;
}

bool wlanApSetupSnapshot(char* outSsid, size_t ssidLen, char* outIp, size_t ipLen) {
    if (outSsid == nullptr || ssidLen == 0U || outIp == nullptr || ipLen == 0U) {
        return false;
    }
    strlcpy(outSsid, kSetupApSsid, ssidLen);
    strlcpy(outIp, kSetupApIp, ipLen);
    if (g_apMode.load(std::memory_order_relaxed) && wlanWifiApiLockTimed(200U)) {
        const IPAddress apIp = WiFi.softAPIP();
        if (apIp[0] != 0) {
            formatIpv4ToBuf(apIp, outIp, ipLen);
        }
        wlanWifiApiUnlock();
    }
    return true;
}

std::atomic<uint8_t> s_lastStaDisconnectReason{0};
static std::atomic<unsigned long> s_bootSettledAtMs{0};

unsigned long wlanBootSettledAtMs() {
    return s_bootSettledAtMs.load(std::memory_order_acquire);
}

void wlanNoteBootSettledNow() {
    unsigned long expected = 0UL;
    const unsigned long now = millis();
    (void)s_bootSettledAtMs.compare_exchange_strong(expected, now == 0UL ? 1UL : now,
                                                    std::memory_order_acq_rel);
}

void wlanNoteCaptiveDnsStarted() {
    s_captiveDnsStarted.store(true, std::memory_order_release);
}

bool wlanArmSetupApMode() {
    if (!wlanEnsureSetupApPass()) {
        ESP_LOGE(TAG, "setup PIN unavailable");
        return false;
    }
    g_apMode.store(true, std::memory_order_relaxed);
    return true;
}

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

bool wlanApplyStaIpConfigLocked(const WlanConfig& cfg) {
    IPAddress dns1(0, 0, 0, 0);
    IPAddress dns2(0, 0, 0, 0);
    uint8_t dns1Oct[4]{};
    uint8_t dns2Oct[4]{};
    const bool customDns = cfg.dns1[0] != '\0' || cfg.dns2[0] != '\0';
    if (cfg.dns1[0] != '\0') {
        if (!parseIpv4Dotted(cfg.dns1, dns1Oct)) {
            return false;
        }
        dns1 = IPAddress(dns1Oct[0], dns1Oct[1], dns1Oct[2], dns1Oct[3]);
    }
    if (cfg.dns2[0] != '\0') {
        if (!parseIpv4Dotted(cfg.dns2, dns2Oct)) {
            return false;
        }
        dns2 = IPAddress(dns2Oct[0], dns2Oct[1], dns2Oct[2], dns2Oct[3]);
    }

    if (cfg.mode != WlanIpMode::Static) {
        if (!customDns) {
            return WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        }
        return WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);
    }

    uint8_t ipOct[4]{};
    uint8_t gwOct[4]{};
    uint8_t maskOct[4]{};
    if (!parseIpv4Dotted(cfg.ip, ipOct) || !parseIpv4Dotted(cfg.gateway, gwOct)
        || !parseIpv4Dotted(cfg.netmask, maskOct)) {
        return false;
    }
    // Static without DNS override → Cloudflare defaults.
    if (!customDns) {
        if (!parseIpv4Dotted(kWifiDefaultDns1, dns1Oct) || !parseIpv4Dotted(kWifiDefaultDns2, dns2Oct)) {
            return false;
        }
        dns1 = IPAddress(dns1Oct[0], dns1Oct[1], dns1Oct[2], dns1Oct[3]);
        dns2 = IPAddress(dns2Oct[0], dns2Oct[1], dns2Oct[2], dns2Oct[3]);
    }
    IPAddress localIp(ipOct[0], ipOct[1], ipOct[2], ipOct[3]);
    IPAddress gateway(gwOct[0], gwOct[1], gwOct[2], gwOct[3]);
    IPAddress netmask(maskOct[0], maskOct[1], maskOct[2], maskOct[3]);
    return WiFi.config(localIp, gateway, netmask, dns1, dns2);
}

bool wlanFillStaNetSnapshot(bool* outConnected, char* ssidBuf, size_t ssidLen, char* ipStr,
                            size_t ipLen, char* gatewayStr, size_t gatewayLen, char* netmaskStr,
                            size_t netmaskLen, char* dns1Str, size_t dns1Len, char* dns2Str,
                            size_t dns2Len, int* outRssi) {
    if (outConnected == nullptr || ssidBuf == nullptr || ipStr == nullptr || gatewayStr == nullptr
        || netmaskStr == nullptr || dns1Str == nullptr || dns2Str == nullptr || outRssi == nullptr
        || ssidLen == 0U || ipLen == 0U || gatewayLen == 0U || netmaskLen == 0U || dns1Len == 0U
        || dns2Len == 0U) {
        return false;
    }
    if (!wlanWifiApiLockTimed(500U)) {
        return false;
    }
    const bool ok = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    *outConnected = ok;
    if (!ok) {
        ssidBuf[0] = ipStr[0] = gatewayStr[0] = netmaskStr[0] = dns1Str[0] = dns2Str[0] = '\0';
        *outRssi = 0;
        wlanWifiApiUnlock();
        return true;
    }
    formatIpv4ToBuf(WiFi.localIP(), ipStr, ipLen);
    formatIpv4ToBuf(WiFi.gatewayIP(), gatewayStr, gatewayLen);
    formatIpv4ToBuf(WiFi.subnetMask(), netmaskStr, netmaskLen);
    formatIpv4ToBuf(WiFi.dnsIP(0), dns1Str, dns1Len);
    formatIpv4ToBuf(WiFi.dnsIP(1), dns2Str, dns2Len);
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

bool wlanFillStaLinkSnapshot(bool* outConnected, char* ipStr, size_t ipLen, char* ssidBuf,
                             size_t ssidLen, int* outRssi) {
    char gateway[kIpv4StrMaxLen]{};
    char netmask[kIpv4StrMaxLen]{};
    char dns1[kIpv4StrMaxLen]{};
    char dns2[kIpv4StrMaxLen]{};
    return wlanFillStaNetSnapshot(outConnected, ssidBuf, ssidLen, ipStr, ipLen, gateway,
                                  sizeof(gateway), netmask, sizeof(netmask), dns1, sizeof(dns1),
                                  dns2, sizeof(dns2), outRssi);
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
    flushHeartCounterIfDirty();
    flushHeartSentCounterIfDirty();
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

void wlanLoop() {
    // Fallback for a full NetCmd queue: WiFi event work is coalesced in atomic flags.
    wlanHandleStaGotIpNetCmd();
    wlanHandleStaReconnectNetCmd();
    wlanBootConnectServiceLoop();
    wifiScanServiceOnMainTask();
    wifiConnectionTestServiceLoop();
    wlanRecoveryServiceLoop();
    if (s_captiveDnsStarted.load(std::memory_order_acquire)) {
        static unsigned long s_lastApDnsPollMs = 0UL;
        const unsigned long  nowMs             = millis();
        const int            apClients         = WiFi.softAPgetStationNum();
        if (apClients > 0 || s_lastApDnsPollMs == 0UL
            || (nowMs - s_lastApDnsPollMs) >= kApDnsPollIntervalMs) {
            g_dnsServer.processNextRequest();
            s_lastApDnsPollMs = nowMs;
        }
    }
    if (s_mdnsRestartNeeded.exchange(false, std::memory_order_acq_rel)) {
        if (!g_apMode.load(std::memory_order_relaxed) && wlanStaConnectedOk()) {
            char staHostname[kDeviceStaHostnameBufLen]{};
            if (!buildDeviceStaHostname(staHostname, sizeof(staHostname))) {
                strlcpy(staHostname, kDeviceHostname, sizeof(staHostname));
                ESP_LOGE(TAG, "Device ID unavailable; using non-unique mDNS hostname");
            }
            MDNS.end();
            if (!MDNS.begin(staHostname)) {
                ESP_LOGW(TAG, "mDNS.begin after GOT_IP failed");
            }
            MDNS.addService("http", "tcp", 80);
        }
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
        // Connected MQTT session: modem sleep is fine.
        WiFi.setSleep(true);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    } else {
        // During reconnect / TLS handshake, keep radio fully awake.
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
    wlanWifiApiUnlock();
}
