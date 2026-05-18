#pragma once

#include <cstddef>
#include <cstdint>

#include "constants.h"

void setupWiFi();
void resetAllSettings();

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

void wlanFillStaLinkSnapshot(bool* outConnected, char* ipStr, size_t ipLen, char* ssidBuf,
                             size_t ssidLen, int* outRssi);

bool wlanLastStaBootFailureSsidSnapshot(char* outSsid, size_t maxLen);

void wlanWifiApiLock();
void wlanWifiApiUnlock();

bool wlanReadStaLocalIpForCommit(char* outIp, size_t ipLen);

/** Queued from WiFi event; run reconnect/backoff under network task. */
void wlanHandleStaReconnectNetCmd();
