#pragma once

#include <cstddef>
#include <cstdint>

#include "wlan_config.h"

void setupWiFi();
void resetAllSettings();

/** True after setupWiFi() finished (STA or AP path). */
auto wlanIsSetupComplete() -> bool;

/** True after boot STA connect attempt finished (connected or AP fallback). */
auto wlanIsBootWifiSettled() -> bool;

/** Persist full WLAN config (SSID/pass + IP mode + NTP). */
auto wlanSaveConfigToNvs(const WlanConfig &cfg) -> bool;

/** Load config; migrates legacy cred_v1 / ssid+pass to DHCP + default NTP. */
auto wlanLoadConfigFromNvs(WlanConfig *cfg) -> bool;

/**
 * Last successful NVS load/save (including AP-mode stored creds).
 * No g_nvsMutex. False if never loaded/saved (caller clears).
 */
bool wlanCopyCachedConfig(WlanConfig *out);

/** @deprecated Prefer wlanSaveConfigToNvs; saves DHCP-only config. */
auto configSaveWiFiCredentials(const char *ssid, const char *password) -> bool;

auto configIsApMode() -> bool;

/** Captive DNS + mDNS; call from main loop. */
void wlanLoop();

struct WlanScanRow {
    char ssid[kWifiSsidMaxLen];
    int rssi;
    bool open;
};

/** Max rows returned by wlanWifiScanCopySnapshot (UI + driver work buffers sized to this). */
constexpr size_t kWlanWifiScanCacheMaxRows = 40;

auto wlanStaConnectedOk() -> bool;

/** STA up long enough after GOT_IP (MQTT/TLS guard). */
auto wlanStaStableForMqtt() -> bool;

auto wlanNtpSynced() -> bool;

/** Modem PS: true when MQTT session up; false helps reconnect after drop. */
void wlanSetStaPowerSaveMqttActive(bool mqttSessionActive);

/** Invalidate the scan snapshot immediately and kick a sweep unless one is running. */
void wlanRequestWifiScanRefresh();

enum class WlanWifiScanStatus : uint8_t {
    Idle,
    Pending,
    Ready,
    Failed,
};

auto wlanWifiScanStatus() -> WlanWifiScanStatus;

auto wlanWifiScanCacheReady() -> bool;

auto wlanWifiScanCopySnapshot(WlanScanRow *out, size_t maxRows) -> size_t;

auto wlanWifiScanCachedCount() -> size_t;

auto wlanWifiScanCopyRowAt(size_t index, WlanScanRow *out) -> bool;

auto wlanFillStaLinkSnapshot(bool *outConnected, char *ipStr, size_t ipLen, char *ssidBuf, size_t ssidLen, int *outRssi) -> bool;

/** Extended STA link snapshot including DHCP-assigned topology. */
auto wlanFillStaNetSnapshot(bool *outConnected, char *ssidBuf, size_t ssidLen, char *ipStr, size_t ipLen, char *gatewayStr,
                            size_t gatewayLen, char *netmaskStr, size_t netmaskLen, char *dns1Str, size_t dns1Len, char *dns2Str,
                            size_t dns2Len, int *outRssi) -> bool;

auto wlanLastStaBootFailureSsidSnapshot(char *outSsid, size_t maxLen) -> bool;

void wlanWifiApiLock();
void wlanWifiApiUnlock();

/** Try WiFi API mutex with timeout; false if unavailable. */
auto wlanWifiApiLockTimed(uint32_t timeoutMs) -> bool;

auto wlanReadStaLocalIpForCommit(char *outIp, size_t ipLen) -> bool;

/** Copy cached STA IPv4 (GOT_IP / disconnect). No WiFi API lock. */
bool wlanCopyCachedStaIp(char *out, size_t len);

/** Apply DHCP or static IPv4 under the WiFi API lock (caller must hold lock). */
auto wlanApplyStaIpConfigLocked(const WlanConfig &cfg) -> bool;

/** Queued from WiFi event; run reconnect/backoff under network task. */
void wlanHandleStaReconnectNetCmd();

/** Queued from WiFi GOT_IP; finish STA setup under the network task. */
void wlanHandleStaGotIpNetCmd();

/** Stage-2 recovery (forced reassoc / guarded restart); call from wlanLoop. */
void wlanRecoveryServiceLoop();

/**
 * Force STA reassociation via disconnect(false)+begin (shared by event escalate + recovery).
 * @param reasonTag short log tag (may be nullptr).
 */
void wlanForceStaReassoc(const char *reasonTag);

/** Controlled restart after prolonged outage (flush counters, stop net services). */
void wlanControlledRestart(const char *reasonTag);

/**
 * Snapshot of SoftAP setup connection data (SSID and IP).
 */
auto wlanApSetupSnapshot(char *outSsid, size_t ssidLen, char *outIp, size_t ipLen) -> bool;

/** SoftAP WPA PSK for WIFI QR (alphanumeric ≥20; not exposed on the HTTP API). */
auto wlanApSetupPassSnapshot(char *outPass, size_t passLen) -> bool;

/** Load or create the SoftAP PSK in NVS and cache it in RAM. */
auto wlanEnsureSetupApPass() -> bool;

/** True when Ensure must mint a new setup PSK (RAM and NVS both invalid). */
inline bool setupApPassShouldGenerate(bool ramSyntaxOk, bool nvsSyntaxOk) {
    return !ramSyntaxOk && !nvsSyntaxOk;
}

/** Cache setup PSK and mark AP mode so the WIFI QR can be painted before RF. */
auto wlanArmSetupApMode() -> bool;

/** Briefly drop WiFi TX while the E-Ink panel refreshes (~15–20 s). */
auto wlanBeginLowInterferenceForEpd() -> bool;
void wlanEndLowInterferenceForEpd();

/** True while an E-Ink full refresh holds the low-interference window. */
auto wlanEpdRefreshActive() -> bool;

/** Wall-clock ms when boot WiFi first settled (0 if not yet). */
auto wlanBootSettledAtMs() -> unsigned long;