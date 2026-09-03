#pragma once

#include "mqtt/config.h"
#include "wifi/wlan_config.h"

#include <ArduinoJson.h>

void fillWifiRuntimeFields(JsonObject obj, const char *ip, const char *gateway, const char *netmask, const char *dns1,
                           const char *dns2, int rssi);
void fillDeviceJson(JsonObject obj);
void fillWifiStatusJson(JsonObject obj);
void fillWifiStatusJson(JsonObject obj, bool connected, const char *ssid, const char *ip, const char *gateway,
                        const char *netmask, const char *dns1, const char *dns2, int rssi);
void fillSettingsJson(JsonObject obj);
void fillChayaJson(JsonObject obj);
void fillChayaJson(JsonObject obj, int rx, int tx, bool connected, bool configured, bool paired);
void fillMqttStatusJson(JsonObject obj, bool connected);
void fillMqttConfigJson(JsonObject obj, const MqttConfig &cfg);
void fillDeviceBatteryJson(JsonObject obj, int mv, int pct);
void fillWifiConfigJson(JsonObject obj, const WlanConfig &cfg);
