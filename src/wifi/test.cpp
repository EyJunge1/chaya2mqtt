#include "test.h"

#include "util/ip_format.h"

#include "async/task_handles.h"
#include "async/web_server_hooks.h"
#include "constants.h"
#include "diag/task_watchdog.h"
#include "identity/device_identity.h"
#include "wlan.h"
#include "wlan_internal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiType.h>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI_TST");

// STA join test in AP mode before NVS commit.

static constexpr unsigned long kWifiConnectionTestTimeoutMs = 15000UL;
static constexpr unsigned long kWifiConnectionTestBeginWaitMs = 30000UL;

static WlanConfig s_wifiConnTestCfg{};
static unsigned long s_wifiConnTestStartMs = 0;
static WlanWifiConnectionTestState s_wifiConnTestState = WlanWifiConnectionTestState::Idle;
// Testing + begin-pending: HTTP queued start; WiFi.begin runs in the service loop.
static bool s_wifiConnTestBeginPending = false;
// Disconnect requested; WiFi.disconnect runs in the service loop only.
static bool s_wifiConnTestAbortPending = false;
// Bumped on abort/reset so an in-flight WiFi.begin can be discarded.
static uint32_t s_wifiConnTestEpoch = 0;

static inline void wifiTestLock() {
    if (g_wifiTestMutex != nullptr) {
        xSemaphoreTake(g_wifiTestMutex, portMAX_DELAY);
    }
}

static inline void wifiTestUnlock() {
    if (g_wifiTestMutex != nullptr) {
        xSemaphoreGive(g_wifiTestMutex);
    }
}

static void disconnectStaIfaceKeepSoftAp() {
    // Disconnect STA only; do not touch NVS. Caller is the network task.
    wlanWifiApiLock();
    if (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_STA) {
        WiFi.disconnect(false, false);
    }
    wlanWifiApiUnlock();
}

static void wifiConnTestResetToIdleLocked() {
    wlanConfigClear(&s_wifiConnTestCfg);
    s_wifiConnTestStartMs = 0;
    s_wifiConnTestState = WlanWifiConnectionTestState::Idle;
    s_wifiConnTestBeginPending = false;
    s_wifiConnTestEpoch += 1U;
}

static bool wifiConnTestRfMayBeUpLocked() {
    return (s_wifiConnTestState == WlanWifiConnectionTestState::Testing && !s_wifiConnTestBeginPending) ||
           (s_wifiConnTestState == WlanWifiConnectionTestState::Ok);
}

static void wifiConnTestIssueBegin(const WlanConfig &cfg) {
    wlanWifiApiLock();
    if (WiFi.getMode() != WIFI_AP_STA && WiFi.getMode() != WIFI_AP) {
        ESP_LOGW(TAG, "wlanStartWifiConnectionTest: unexpected WiFi mode");
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    char staHostname[kDeviceStaHostnameBufLen]{};
    if (!buildDeviceStaHostname(staHostname, sizeof(staHostname))) {
        strlcpy(staHostname, kDeviceHostname, sizeof(staHostname));
        ESP_LOGE(TAG, "Device ID unavailable; using non-unique STA hostname");
    }
    WiFi.setHostname(staHostname);
    if (!wlanApplyStaIpConfigLocked(cfg)) {
        ESP_LOGW(TAG, "WiFi.config failed during test — falling back to DHCP");
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    if (cfg.ntp1[0] == '\0') {
        wlanEnableDhcpNtpRequest();
    }
    chayaTaskWatchdogUnsubscribe(TAG);
    WiFi.begin(cfg.ssid, cfg.pass);
    chayaTaskWatchdogSubscribe(TAG);
    wlanWifiApiUnlock();
}

void wifiConnectionTestServiceLoop() {
    wifiTestLock();
    if (s_wifiConnTestAbortPending) {
        s_wifiConnTestAbortPending = false;
        disconnectStaIfaceKeepSoftAp();
    }
    if (s_wifiConnTestBeginPending) {
        if (s_wifiScanInProgress.load(std::memory_order_acquire) || wlanEpdRefreshActive()) {
            const unsigned long started = s_wifiConnTestStartMs;
            if (started != 0UL && (millis() - started) > kWifiConnectionTestBeginWaitMs) {
                s_wifiConnTestBeginPending = false;
                s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
                wifiTestUnlock();
                ESP_LOGW(TAG, "WLAN connection test timeout waiting for scan/EPD");
                return;
            }
            wifiTestUnlock();
            return;
        }
        if (s_wifiConnTestState != WlanWifiConnectionTestState::Testing) {
            s_wifiConnTestBeginPending = false;
            wifiTestUnlock();
            return;
        }
        const WlanConfig cfg = s_wifiConnTestCfg;
        const uint32_t epoch = s_wifiConnTestEpoch;
        s_wifiConnTestBeginPending = false;
        s_wifiConnTestStartMs = millis();
        wifiTestUnlock();
        wifiConnTestIssueBegin(cfg);
        wifiTestLock();
        if (s_wifiConnTestEpoch != epoch || s_wifiConnTestState != WlanWifiConnectionTestState::Testing) {
            wifiTestUnlock();
            disconnectStaIfaceKeepSoftAp();
            return;
        }
        wifiTestUnlock();
        ESP_LOGI(TAG, "WLAN connection test started for SSID \"%s\" (%s)", cfg.ssid,
                 cfg.mode == WlanIpMode::Static ? "static" : "dhcp");
        return;
    }
    if (s_wifiConnTestState != WlanWifiConnectionTestState::Testing) {
        wifiTestUnlock();
        return;
    }
    wlanWifiApiLock();
    const wl_status_t wst = WiFi.status();
    const bool haveIp = (wst == WL_CONNECTED && WiFi.localIP()[0] != 0);
    char ipStr[16]{};
    if (haveIp) {
        formatIpv4ToBuf(WiFi.localIP(), ipStr, sizeof(ipStr));
    }
    wlanWifiApiUnlock();
    if (haveIp) {
        s_wifiConnTestState = WlanWifiConnectionTestState::Ok;
        wifiTestUnlock();
        ESP_LOGI(TAG, "WLAN connection test OK, IP %s", ipStr);
        // NVS save happens at POST /wifi-connect-commit.
        return;
    }
    // Fail fast on definitive WiFi status.
    if (wst == WL_NO_SSID_AVAIL || wst == WL_CONNECT_FAILED || wst == WL_CONNECTION_LOST) {
        disconnectStaIfaceKeepSoftAp();
        s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
        wifiTestUnlock();
        ESP_LOGW(TAG, "WLAN connection test failed (status=%d)", static_cast<int>(wst));
        return;
    }
    const unsigned long started = s_wifiConnTestStartMs;
    if (millis() - started > kWifiConnectionTestTimeoutMs) {
        disconnectStaIfaceKeepSoftAp();
        s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
        wifiTestUnlock();
        ESP_LOGW(TAG, "WLAN connection test timeout");
        return;
    }
    wifiTestUnlock();
}

bool wlanWifiConnectionTestSsidSnapshot(char *outSsid, size_t maxLen) {
    if (outSsid == nullptr || maxLen == 0U) {
        return false;
    }
    wifiTestLock();
    const WlanWifiConnectionTestState st = s_wifiConnTestState;
    if (st == WlanWifiConnectionTestState::Idle) {
        wifiTestUnlock();
        outSsid[0] = '\0';
        return false;
    }
    strlcpy(outSsid, s_wifiConnTestCfg.ssid, maxLen);
    wifiTestUnlock();
    return true;
}

WlanWifiConnectionTestState wlanGetWifiConnectionTestState() {
    wifiTestLock();
    const WlanWifiConnectionTestState st = s_wifiConnTestState;
    wifiTestUnlock();
    return st;
}

void wlanAbortWifiConnectionTest() {
    wifiTestLock();
    if (s_wifiConnTestState == WlanWifiConnectionTestState::Idle && !s_wifiConnTestBeginPending) {
        wifiTestUnlock();
        return;
    }
    if (wifiConnTestRfMayBeUpLocked()) {
        s_wifiConnTestAbortPending = true;
    }
    wifiConnTestResetToIdleLocked();
    wifiTestUnlock();
}

bool wlanRetryWifiConnectionTest() {
    wifiTestLock();
    if (s_wifiConnTestState != WlanWifiConnectionTestState::Fail || s_wifiConnTestCfg.ssid[0] == '\0') {
        wifiTestUnlock();
        return false;
    }
    const WlanConfig cfgCopy = s_wifiConnTestCfg;
    wifiTestUnlock();
    return wlanStartWifiConnectionTest(cfgCopy);
}

bool wlanStartWifiConnectionTest(const WlanConfig &cfg) {
    if (!configIsApMode() || cfg.ssid[0] == '\0' || wlanConfigValidate(&cfg) != nullptr) {
        return false;
    }
    if (wlanEpdRefreshActive()) {
        ESP_LOGW(TAG, "WLAN connection test deferred: EPD refresh active");
        return false;
    }
    if (s_wifiScanInProgress.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "WLAN connection test deferred: scan in progress");
        return false;
    }
    wlanAbortWifiConnectionTest();

    wifiTestLock();
    // Close the race with a refresh or scan beginning after the first check.
    if (wlanEpdRefreshActive() || s_wifiScanInProgress.load(std::memory_order_acquire)) {
        wifiTestUnlock();
        ESP_LOGW(TAG, "WLAN connection test deferred: EPD refresh or scan started");
        return false;
    }
    s_wifiConnTestCfg = cfg;
    s_wifiConnTestStartMs = millis();
    s_wifiConnTestState = WlanWifiConnectionTestState::Testing;
    s_wifiConnTestBeginPending = true;
    wifiTestUnlock();
    return true;
}

bool wlanCommitWifiConnectionTestAndScheduleReboot() {
    wifiTestLock();
    if (s_wifiConnTestState != WlanWifiConnectionTestState::Ok) {
        wifiTestUnlock();
        return false;
    }
    const WlanConfig cfgCopy = s_wifiConnTestCfg;
    char ipProbe[16]{};
    if (!wlanReadStaLocalIpForCommit(ipProbe, sizeof(ipProbe))) {
        wifiTestUnlock();
        ESP_LOGW(TAG, "WLAN commit refused: STA not connected");
        return false;
    }
    if (!wlanSaveConfigToNvs(cfgCopy)) {
        wifiTestUnlock();
        ESP_LOGW(TAG, "WLAN commit: wlanSaveConfigToNvs failed (NVS full?)");
        return false;
    }
    s_wifiConnTestAbortPending = true;
    wifiConnTestResetToIdleLocked();
    wifiTestUnlock();
    webRequestRebootAfterWifiSave();
    return true;
}
