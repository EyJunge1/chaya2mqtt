#pragma once

#include "async/event_types.h"
#include "mqtt/config.h"
#include "web/json_payloads.h"
#include "web/web_middleware.h"
#include "wifi/wlan_config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <cstddef>

void sendOk(AsyncWebServerRequest *req, int code = 200, const char *message = nullptr, const char *next = nullptr);
void sendOkQueued(AsyncWebServerRequest *req, int code, bool queued);
void sendErr(AsyncWebServerRequest *req, int code, const char *error);
bool adminJsonRequireObject(AsyncWebServerRequest *req, JsonVariant &json);

AsyncCallbackJsonWebHandler &adminAddJsonPost(AsyncWebServer &ws, const char *uri, ArJsonRequestHandlerFunction fn);

void handleApiCsrfGet(AsyncWebServerRequest *req);
void handleApiDeviceGet(AsyncWebServerRequest *req);

/** Aggregated boot snapshot: csrf + device (+ STA extras). */
void handleApiBootstrapGet(AsyncWebServerRequest *req);

void handleApiChayaGet(AsyncWebServerRequest *req);
void handleApiChayaSendPost(AsyncWebServerRequest *req, JsonVariant &json);

void handleApiWifiStatusGet(AsyncWebServerRequest *req);
void handleApiWifiConfigGet(AsyncWebServerRequest *req);
void handleApiWifiScanGet(AsyncWebServerRequest *req);
void handleApiWifiScanPost(AsyncWebServerRequest *req, JsonVariant &json);
void handleApiWifiConnectPost(AsyncWebServerRequest *req, JsonVariant &json);
void handleApiWifiConnectStatusGet(AsyncWebServerRequest *req);
void handleApiWifiConnectCommitPost(AsyncWebServerRequest *req, JsonVariant &json);
void handleApiWifiConnectAbortPost(AsyncWebServerRequest *req, JsonVariant &json);
void handleApiWifiConnectRetryPost(AsyncWebServerRequest *req, JsonVariant &json);

void handleApiMqttStatusGet(AsyncWebServerRequest *req);
void handleApiMqttGet(AsyncWebServerRequest *req);
void normalizePartnerIdInput(char *id, size_t idLen);
void handleApiMqttPost(AsyncWebServerRequest *req, JsonVariant &json);

void handleApiSettingsGet(AsyncWebServerRequest *req);
void handleApiSettingsPost(AsyncWebServerRequest *req, JsonVariant &json);

void handleApiRebootPost(AsyncWebServerRequest *req, JsonVariant &json);
void handleApiResetPost(AsyncWebServerRequest *req, NetCmd cmd, const char *message);

void handleApiUpdateStatusGet(AsyncWebServerRequest *req);
void handleApiUpdateCheckPost(AsyncWebServerRequest *req, JsonVariant &json);
void handleApiUpdateInstallPost(AsyncWebServerRequest *req, JsonVariant &json);

void adminRoutesRegisterApiDevice(AsyncWebServer &ws);
void adminRoutesRegisterApiChaya(AsyncWebServer &ws);
void adminRoutesRegisterApiWifi(AsyncWebServer &ws);
void adminRoutesRegisterApiMqtt(AsyncWebServer &ws);
void adminRoutesRegisterApiSettings(AsyncWebServer &ws);
void adminRoutesRegisterApiSystem(AsyncWebServer &ws);
void adminRoutesRegisterApiOta(AsyncWebServer &ws);
