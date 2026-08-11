#pragma once

#include <cstddef>
#include <cstdint>

#include "wlan_config.h"

void setupWiFi();
void resetAllSettings();

/** True after setupWiFi() finished (STA or AP path). */
bool wlanIsSetupComplete();

/** True after boot STA connect attempt finished (connected or AP fallback). */
bool wlanIsBootWifiSettled();

void releaseGpioHoldBeforeRestart();

/** Persist full WLAN config (SSID/pass + IP mode + NTP). */
bool wlanSaveConfigToNvs(const WlanConfig& cfg);

/** Load config; migrates legacy cred_v1 / ssid+pass to DHCP + default NTP. */
bool wlanLoadConfigFromNvs(WlanConfig* cfg);

/** @deprecated Prefer wlanSaveConfigToNvs; saves DHCP-only config. */
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

bool wlanFillStaLinkSnapshot(bool* outConnected, char* ipStr, size_t ipLen, char* ssidBuf,
                             size_t ssidLen, int* outRssi);

/** Extended STA link snapshot including DHCP-assigned topology. */
bool wlanFillStaNetSnapshot(bool* outConnected, char* ssidBuf, size_t ssidLen, char* ipStr,
                            size_t ipLen, char* gatewayStr, size_t gatewayLen, char* netmaskStr,
                            size_t netmaskLen, char* dns1Str, size_t dns1Len, char* dns2Str,
                            size_t dns2Len, int* outRssi);

bool wlanLastStaBootFailureSsidSnapshot(char* outSsid, size_t maxLen);

void wlanWifiApiLock();
void wlanWifiApiUnlock();

/** Try WiFi API mutex with timeout; false if unavailable. */
bool wlanWifiApiLockTimed(uint32_t timeoutMs);

bool wlanReadStaLocalIpForCommit(char* outIp, size_t ipLen);

/** Apply DHCP or static IPv4 under the WiFi API lock (caller must hold lock). */
bool wlanApplyStaIpConfigLocked(const WlanConfig& cfg);

/** Queued from WiFi event; run reconnect/backoff under network task. */
void wlanHandleStaReconnectNetCmd();

/** Stage-2 recovery (forced reassoc / guarded restart); call from wlanLoop. */
void wlanRecoveryServiceLoop();

/**
 * Force STA reassociation via disconnect(false)+begin (shared by event escalate + recovery).
 * @param reasonTag short log tag (may be nullptr).
 */
void wlanForceStaReassoc(const char* reasonTag);

/** Controlled restart after prolonged outage (flush counters, stop net services). */
void wlanControlledRestart(const char* reasonTag);

/**
 * Snapshot of open SoftAP setup connection data (SSID and IP).
 */
bool wlanApSetupSnapshot(char* outSsid, size_t ssidLen, char* outIp, size_t ipLen);

/** Wall-clock ms when boot WiFi first settled (0 if not yet). */
unsigned long wlanBootSettledAtMs();
