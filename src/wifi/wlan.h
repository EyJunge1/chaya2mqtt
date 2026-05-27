#pragma once

#include <cstddef>
#include <cstdint>

#include "constants.h"

void setupWiFi();
void resetAllSettings();

/** True after setupWiFi() finished (STA or AP path). */
bool wlanIsSetupComplete();

/** True after boot STA connect attempt finished (connected or AP fallback). */
bool wlanIsBootWifiSettled();

void releaseGpioHoldBeforeRestart();

bool configSaveWiFiCredentials(const char* ssid, const char* password);

bool configIsApMode();

/** Captive DNS + mDNS; call from main loop. */
void wlanLoop();

struct WlanScanRow {
    char ssid[kWifiSsidMaxLen];
    int  rssi;
    bool open;
};

/** Max rows returned by wlanWifiScanCopySnapshot (UI + driver work buffers sized to this). */
constexpr size_t kWlanWifiScanCacheMaxRows = 40;

bool wlanStaConnectedOk();

/** STA up long enough after GOT_IP (MQTT/TLS guard). */
bool wlanStaStableForMqtt();

bool wlanNtpSynced();

/** Modem PS: true when MQTT session up; false helps reconnect after drop. */
void wlanSetStaPowerSaveMqttActive(bool mqttSessionActive);

void wlanRequestWifiScanRefresh();

bool wlanWifiScanCacheReady();

size_t wlanWifiScanCopySnapshot(WlanScanRow* out, size_t maxRows);

size_t wlanWifiScanCachedCount();

bool wlanWifiScanCopyRowAt(size_t index, WlanScanRow* out);

void wlanFillStaLinkSnapshot(bool* outConnected, char* ipStr, size_t ipLen, char* ssidBuf,
                             size_t ssidLen, int* outRssi);

bool wlanLastStaBootFailureSsidSnapshot(char* outSsid, size_t maxLen);

void wlanWifiApiLock();
void wlanWifiApiUnlock();

/** Try WiFi API mutex with timeout; false if unavailable. */
bool wlanWifiApiLockTimed(uint32_t timeoutMs);

bool wlanReadStaLocalIpForCommit(char* outIp, size_t ipLen);

/** Queued from WiFi event; run reconnect/backoff under network task. */
void wlanHandleStaReconnectNetCmd();
