#pragma once

#include "async/event_types.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstddef>

void sendOk(AsyncWebServerRequest *req, int code = 200, const char *extraJson = nullptr);
void sendErr(AsyncWebServerRequest *req, int code, const char *error);
bool parseFormIntStrict(const String &text, int *out);

void handleApiCsrfGet(AsyncWebServerRequest *req);
void handleApiDeviceGet(AsyncWebServerRequest *req);

/** Aggregated boot snapshot: csrf + device (+ STA extras). */
void handleApiBootstrapGet(AsyncWebServerRequest *req);

void handleApiChayaGet(AsyncWebServerRequest *req);
void handleApiChayaSendPost(AsyncWebServerRequest *req);

bool appendWifiRuntimeFields(char *body, size_t bodyLen, size_t *pos, const char *ip, const char *gateway, const char *netmask,
                             const char *dns1, const char *dns2, int rssi);
void handleApiWifiStatusGet(AsyncWebServerRequest *req);
void handleApiWifiConfigGet(AsyncWebServerRequest *req);
void handleApiWifiScanGet(AsyncWebServerRequest *req);
void handleApiWifiScanPost(AsyncWebServerRequest *req);
void handleApiWifiConnectPost(AsyncWebServerRequest *req);
void handleApiWifiConnectStatusGet(AsyncWebServerRequest *req);
void handleApiWifiConnectCommitPost(AsyncWebServerRequest *req);
void handleApiWifiConnectAbortPost(AsyncWebServerRequest *req);

void handleApiMqttStatusGet(AsyncWebServerRequest *req);
void handleApiMqttGet(AsyncWebServerRequest *req);
void normalizePartnerIdInput(char *id, size_t idLen);
void handleApiMqttPost(AsyncWebServerRequest *req);

void handleApiSettingsGet(AsyncWebServerRequest *req);
void handleApiSettingsPost(AsyncWebServerRequest *req);

void handleApiRebootPost(AsyncWebServerRequest *req);
void handleApiResetPost(AsyncWebServerRequest *req, NetCmd cmd, const char *message);

void handleApiUpdateStatusGet(AsyncWebServerRequest *req);
void handleApiUpdateCheckPost(AsyncWebServerRequest *req);
void handleApiUpdateInstallPost(AsyncWebServerRequest *req);

void adminRoutesRegisterApiDevice(AsyncWebServer &ws);
void adminRoutesRegisterApiChaya(AsyncWebServer &ws);
void adminRoutesRegisterApiWifi(AsyncWebServer &ws);
void adminRoutesRegisterApiMqtt(AsyncWebServer &ws);
void adminRoutesRegisterApiSettings(AsyncWebServer &ws);
void adminRoutesRegisterApiSystem(AsyncWebServer &ws);
void adminRoutesRegisterApiOta(AsyncWebServer &ws);
