#pragma once

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

/**
 * STA connected with usable IPv4 (avoids including Arduino WiFi.h in mqtt/ota — IDE/clang friendly).
 */
bool wlanStaConnectedOk();
