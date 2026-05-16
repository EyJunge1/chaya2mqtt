#include "test.h"

#include "constants.h"
#include "wlan.h"

#include "web/admin.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiType.h>
#include <cstring>
#include <esp_log.h>

#include "log_tag.h"

DEFINE_LOG_TAG("WIFI");

static constexpr unsigned long kWifiConnectionTestTimeoutMs = 15000UL;

static char                  s_wifiConnTestSsid[kWifiSsidMaxLen]{};
static char                  s_wifiConnTestPass[kWifiPassMaxLen]{};
static unsigned long       s_wifiConnTestStartMs       = 0;
static WlanWifiConnectionTestState s_wifiConnTestState = WlanWifiConnectionTestState::Idle;

static void disconnectStaIfaceKeepSoftAp() {
    /* Do not wipe credentials in NVS — only detach STA radio from the AP/network. */
    if (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_STA) {
        WiFi.disconnect(false, false);
    }
}

void wifiConnectionTestServiceLoop() {
    if (s_wifiConnTestState != WlanWifiConnectionTestState::Testing) {
        return;
    }
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED && WiFi.localIP()[0] != 0) {
        ESP_LOGI(TAG, "WLAN connection test OK, IP %s", WiFi.localIP().toString().c_str());
        s_wifiConnTestState = WlanWifiConnectionTestState::Ok;
        if (!configSaveWiFiCredentials(s_wifiConnTestSsid, s_wifiConnTestPass)) {
            ESP_LOGE(TAG, "WLAN connection test: NVS credentials save failed");
            disconnectStaIfaceKeepSoftAp();
            s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
            return;
        }
        /* Reboot from POST /wifi-connect-commit so the browser can poll state ok before restart. */
        return;
    }
    /* Treat definitive failure statuses without waiting full timeout */
    if (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED || st == WL_CONNECTION_LOST) {
        ESP_LOGW(TAG, "WLAN connection test failed (status=%d)", static_cast<int>(st));
        disconnectStaIfaceKeepSoftAp();
        s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
        return;
    }
    if (millis() - s_wifiConnTestStartMs > kWifiConnectionTestTimeoutMs) {
        ESP_LOGW(TAG, "WLAN connection test timeout");
        disconnectStaIfaceKeepSoftAp();
        s_wifiConnTestState = WlanWifiConnectionTestState::Fail;
    }
}

bool wlanWifiConnectionTestSsidSnapshot(char* outSsid, size_t maxLen) {
    if (outSsid == nullptr || maxLen == 0U) {
        return false;
    }
    if (s_wifiConnTestState == WlanWifiConnectionTestState::Idle) {
        outSsid[0] = '\0';
        return false;
    }
    strlcpy(outSsid, s_wifiConnTestSsid, maxLen);
    return true;
}

WlanWifiConnectionTestState wlanGetWifiConnectionTestState() {
    return s_wifiConnTestState;
}

void wlanAbortWifiConnectionTest() {
    if (s_wifiConnTestState == WlanWifiConnectionTestState::Idle) {
        return;
    }
    disconnectStaIfaceKeepSoftAp();
    memset(s_wifiConnTestSsid, 0, sizeof(s_wifiConnTestSsid));
    memset(s_wifiConnTestPass, 0, sizeof(s_wifiConnTestPass));
    s_wifiConnTestStartMs = 0;
    s_wifiConnTestState   = WlanWifiConnectionTestState::Idle;
}

bool wlanStartWifiConnectionTest(const char* ssid, const char* password) {
    if (!configIsApMode() || ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    wlanAbortWifiConnectionTest();

    strlcpy(s_wifiConnTestSsid, ssid, sizeof(s_wifiConnTestSsid));
    strlcpy(s_wifiConnTestPass, password != nullptr ? password : "", sizeof(s_wifiConnTestPass));

    if (WiFi.getMode() != WIFI_AP_STA && WiFi.getMode() != WIFI_AP) {
        /* Should not happen in AP setup */
        ESP_LOGW(TAG, "wlanStartWifiConnectionTest: unexpected WiFi mode");
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setHostname(kDeviceHostname);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.begin(s_wifiConnTestSsid, s_wifiConnTestPass);

    s_wifiConnTestStartMs = millis();
    s_wifiConnTestState   = WlanWifiConnectionTestState::Testing;
    ESP_LOGI(TAG, "WLAN connection test started for SSID \"%s\"", s_wifiConnTestSsid);
    return true;
}

bool wlanCommitWifiConnectionTestAndScheduleReboot() {
    if (s_wifiConnTestState != WlanWifiConnectionTestState::Ok) {
        return false;
    }
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP()[0] == 0) {
        ESP_LOGW(TAG, "WLAN commit refused: STA not connected");
        return false;
    }
    if (!configSaveWiFiCredentials(s_wifiConnTestSsid, s_wifiConnTestPass)) {
        return false;
    }
    wlanAbortWifiConnectionTest();
    webAdminScheduleWifiConfiguredReboot();
    return true;
}
