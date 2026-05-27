#include "test.h"

#include "util/ip_format.h"

#include "async/task_handles.h"
#include "constants.h"
#include "web/deferred_reboot.h"
#include "wlan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiType.h>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI_TST");

// STA join test in AP mode before NVS commit.

static constexpr unsigned long kWifiConnectionTestTimeoutMs = 15000UL;

static char                        s_wifiConnTestSsid[kWifiSsidMaxLen]{};
static char                        s_wifiConnTestPass[kWifiPassMaxLen]{};
static unsigned long             s_wifiConnTestStartMs       = 0;
static WlanWifiConnectionTestState s_wifiConnTestState = WlanWifiConnectionTestState::Idle;

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
    // Disconnect STA only; do not touch NVS.
    wlanWifiApiLock();
    if (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_STA) {
        WiFi.disconnect(false, false);
    }
    wlanWifiApiUnlock();
}

void wifiConnectionTestServiceLoop() {
    wifiTestLock();
    const WlanWifiConnectionTestState st = s_wifiConnTestState;
    wifiTestUnlock();

    if (st != WlanWifiConnectionTestState::Testing) {
        return;
    }
    wlanWifiApiLock();
    const wl_status_t wst = WiFi.status();
    const bool          haveIp =
        (wst == WL_CONNECTED && WiFi.localIP()[0] != 0);
    char ipStr[16]{};
    if (haveIp) {
        formatIpv4ToBuf(WiFi.localIP(), ipStr, sizeof(ipStr));
    }
    wlanWifiApiUnlock();
    if (haveIp) {
        ESP_LOGI(TAG, "WLAN connection test OK, IP %s", ipStr);
        wifiTestLock();
        s_wifiConnTestState = WlanWifiConnectionTestState::Ok;
        wifiTestUnlock();
        // NVS save happens at POST /wifi-connect-commit.
        return;
    }
    // Fail fast on definitive WiFi status.
    wlanWifiApiLock();
    const wl_status_t wst2 = WiFi.status();
    wlanWifiApiUnlock();
    if (wst2 == WL_NO_SSID_AVAIL || wst2 == WL_CONNECT_FAILED || wst2 == WL_CONNECTION_LOST) {
        ESP_LOGW(TAG, "WLAN connection test failed (status=%d)", static_cast<int>(wst2));
        disconnectStaIfaceKeepSoftAp();
        wifiTestLock();
        s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
        wifiTestUnlock();
        return;
    }
    wifiTestLock();
    const unsigned long started = s_wifiConnTestStartMs;
    wifiTestUnlock();
    if (millis() - started > kWifiConnectionTestTimeoutMs) {
        ESP_LOGW(TAG, "WLAN connection test timeout");
        disconnectStaIfaceKeepSoftAp();
        wifiTestLock();
        s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
        wifiTestUnlock();
    }
}

bool wlanWifiConnectionTestSsidSnapshot(char* outSsid, size_t maxLen) {
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
    strlcpy(outSsid, s_wifiConnTestSsid, maxLen);
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
    if (s_wifiConnTestState == WlanWifiConnectionTestState::Idle) {
        wifiTestUnlock();
        return;
    }
    wifiTestUnlock();
    disconnectStaIfaceKeepSoftAp();
    wifiTestLock();
    memset(s_wifiConnTestSsid, 0, sizeof(s_wifiConnTestSsid));
    memset(s_wifiConnTestPass, 0, sizeof(s_wifiConnTestPass));
    s_wifiConnTestStartMs = 0;
    s_wifiConnTestState   = WlanWifiConnectionTestState::Idle;
    wifiTestUnlock();
}

bool wlanStartWifiConnectionTest(const char* ssid, const char* password) {
    if (!configIsApMode() || ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    wlanAbortWifiConnectionTest();

    wifiTestLock();
    strlcpy(s_wifiConnTestSsid, ssid, sizeof(s_wifiConnTestSsid));
    strlcpy(s_wifiConnTestPass, password != nullptr ? password : "", sizeof(s_wifiConnTestPass));
    s_wifiConnTestStartMs = millis();
    s_wifiConnTestState   = WlanWifiConnectionTestState::Testing;
    wifiTestUnlock();

    wlanWifiApiLock();
    if (WiFi.getMode() != WIFI_AP_STA && WiFi.getMode() != WIFI_AP) {
        // Unexpected in AP-only setup.
        ESP_LOGW(TAG, "wlanStartWifiConnectionTest: unexpected WiFi mode");
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setHostname(kDeviceHostname);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.begin(s_wifiConnTestSsid, s_wifiConnTestPass);
    wlanWifiApiUnlock();

    ESP_LOGI(TAG, "WLAN connection test started for SSID \"%s\"", s_wifiConnTestSsid);
    return true;
}

bool wlanCommitWifiConnectionTestAndScheduleReboot() {
    wifiTestLock();
    const WlanWifiConnectionTestState st = s_wifiConnTestState;
    char ssidCopy[kWifiSsidMaxLen]{};
    char passCopy[kWifiPassMaxLen]{};
    if (st == WlanWifiConnectionTestState::Ok) {
        strlcpy(ssidCopy, s_wifiConnTestSsid, sizeof(ssidCopy));
        strlcpy(passCopy, s_wifiConnTestPass, sizeof(passCopy));
    }
    wifiTestUnlock();

    if (st != WlanWifiConnectionTestState::Ok) {
        return false;
    }
    char ipProbe[16]{};
    if (!wlanReadStaLocalIpForCommit(ipProbe, sizeof(ipProbe))) {
        ESP_LOGW(TAG, "WLAN commit refused: STA not connected");
        return false;
    }
    if (!configSaveWiFiCredentials(ssidCopy, passCopy)) {
        ESP_LOGW(TAG, "WLAN commit: configSaveWiFiCredentials failed (NVS full?)");
        return false;
    }
    wlanAbortWifiConnectionTest();
    deferredRebootAfterWifiSave();
    return true;
}
