#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

class AsyncWebServerRequest;

extern std::atomic<bool> g_webAdminRebootRequested;
extern std::atomic<bool> g_webAdminWifiReconnectRequested;
extern std::atomic<uint32_t> g_webAdminMqttApplyVersion;
extern std::atomic<bool> g_webAdminSettingsApplyPending;
extern std::atomic<bool> g_webAdminMqttNvsWriteFailed;
extern std::atomic<bool> g_webAdminSettingsNvsWriteFailed;
extern std::atomic<bool> g_systemShutdownInProgress;

extern uint8_t      g_webAdminPendingResetDays;
extern char         g_webAdminPendingUiLang[3];
extern char         g_webAdminPendingUiTheme[6];
extern portMUX_TYPE g_webAdminSettingsPendingMux;

bool adminParseBodyParam(AsyncWebServerRequest* req, const char* name, char* out,
                         size_t outLen);
