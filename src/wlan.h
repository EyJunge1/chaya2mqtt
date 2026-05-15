#pragma once

#include <cstddef>
#include <cstdint>

void setupWiFi();
void resetAllSettings();

/** Release known gpio_hold_en before ESP.restart() (strapping / light-sleep holds). */
void releaseGpioHoldBeforeRestart();

/** Save WLAN credentials (NVS namespace wifi). */
bool configSaveWiFiCredentials(const char* ssid, const char* password);

/** SoftAP setup mode (captive portal fallback). */
bool configIsApMode();

/**
 * Captive DNS (AP) and mDNS maintenance — call each main loop iteration.
 */
void wlanLoop();

/** One row from the last completed scan (WLAN module copies into this for HTTP handlers). */
struct WlanScanRow {
    char ssid[33];
    int  rssi;
    bool open;
};

/**
 * STA connected with usable IPv4 (avoids including Arduino WiFi.h in mqtt/ota — IDE/clang friendly).
 */
bool wlanStaConnectedOk();

/** True when wall clock looks plausible (SNTP finished); needed before TLS cert verification. */
bool wlanNtpSynced();

/**
 * When active, apply WiFi modem power save (STA only). Call false when MQTT disconnects so STA
 * can reconnect reliably; call true after a successful MQTT connection.
 */
void wlanSetStaPowerSaveMqttActive(bool mqttSessionActive);

/** Request background WiFi scan (executed in wlanLoop on main task only). */
void wlanRequestWifiScanRefresh();

/** True when cached scan results are ready for /wifi-scan JSON. */
bool wlanWifiScanCacheReady();

/** Copies up to maxRows SSIDs from cache; returns number copied (may be 0). */
size_t wlanWifiScanCopySnapshot(WlanScanRow* out, size_t maxRows);

/** Last boot: STA connect with stored credentials failed; AP fallback (empty if none). */
bool wlanLastStaBootFailureSsidSnapshot(char* outSsid, size_t maxLen);

/** Wi‑Fi credential test during AP setup (before NVS commit). */
enum class WlanWifiConnectionTestState : uint8_t {
    Idle    = 0,
    Testing = 1,
    Ok      = 2,
    Fail    = 3,
};

/** Start STA join test while softAP stays up (AP mode only). */
bool wlanStartWifiConnectionTest(const char* ssid, const char* password);

/** Stop test, disconnect STA interface, reset to Idle. */
void wlanAbortWifiConnectionTest();

WlanWifiConnectionTestState wlanGetWifiConnectionTestState();

/** SSID currently being tested or last result context; false if Idle. */
bool wlanWifiConnectionTestSsidSnapshot(char* outSsid, size_t maxLen);

/** If state Ok and STA still has IPv4: write NVS, schedule reboot. */
bool wlanCommitWifiConnectionTestAndScheduleReboot();
