#pragma once

#include "wlan.h"
#include "wlan_config.h"

#include <Arduino.h>
#include <WiFi.h>

#include <atomic>
#include <cstddef>

#include <DNSServer.h>
#include <freertos/portmacro.h>

extern char         g_lastFailedBootSsid[kWifiSsidMaxLen];
extern portMUX_TYPE g_lastFailedBootSsidMux;

extern DNSServer              g_dnsServer;
extern std::atomic<bool>      g_apMode;

extern std::atomic<unsigned long> s_wifiReconnectNextAllowedMs;
extern std::atomic<uint32_t>      s_wifiReconnectFailCount;
extern std::atomic<bool>          s_mdnsRestartNeeded;

extern std::atomic<bool> s_wifiSetupComplete;
extern std::atomic<bool> s_bootStaConnectPending;
extern std::atomic<bool> s_bootWifiSettled;
extern std::atomic<bool> s_bootStaFinishDone;
extern char              s_bootAttemptSsid[kWifiSsidMaxLen];
extern unsigned long     s_bootStaConnectStartMs;

extern std::atomic<unsigned long> s_staLastGotIpWallMs;

extern WlanScanRow       s_wifiScanCache[kWlanWifiScanCacheMaxRows];
extern WlanScanRow       s_wifiScanRowWork[kWlanWifiScanCacheMaxRows];
extern size_t            s_wifiScanCacheCount;
extern std::atomic<bool> s_wifiScanKick;
extern std::atomic<bool> s_wifiScanInProgress;
extern std::atomic<bool> s_wifiScanHasValidCache;
extern portMUX_TYPE      s_wifiScanCacheMux;

extern std::atomic<unsigned long> s_lastWifiScanKickMs;
extern std::atomic<unsigned long> s_wifiScanNextAllowedMs;

void setupWifiFinishStaConnected();
void setupWifiStartApFallback(const char* attemptedSsid);
void setupWifiBeginStaConnectAsync(const char* ssid, const char* pass);

void wifiLoadCredentialsFromNvs(char* ssid, size_t ssidLen, char* pass, size_t passLen);

void wlanBootConnectServiceLoop();
void wifiScanServiceOnMainTask();
void wifiStationEvent(arduino_event_id_t event);
