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
extern std::atomic<bool>          s_staReconnectWorkPending;
extern std::atomic<bool>          s_staGotIpWorkPending;
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
void setupWifiBeginStaConnectAsync(const WlanConfig& cfg);

void wifiLoadCredentialsFromNvs(char* ssid, size_t ssidLen, char* pass, size_t passLen);

void wlanBootConnectServiceLoop();
void wifiScanServiceOnMainTask();
void wifiStationEvent(arduino_event_id_t event, arduino_event_info_t info);

/** Last STA disconnect reason code (0 if none / lost-IP synthetic). */
extern std::atomic<uint8_t> s_lastStaDisconnectReason;

/** Last loaded boot/test config (NTP apply after GOT_IP). */
extern WlanConfig s_activeWlanConfig;

/** Request DHCP option 42 NTP before STA gets an IP (no-op if unsupported). */
void wlanEnableDhcpNtpRequest();

/** Apply override NTP or automatic DHCP+fallback servers after STA is up. */
void wlanApplyNtpFromConfig(const WlanConfig& cfg);

/** Record first boot-settled timestamp (idempotent). */
void wlanNoteBootSettledNow();

void wlanNoteCaptiveDnsStarted();
