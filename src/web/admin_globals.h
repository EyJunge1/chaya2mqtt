#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

class AsyncWebServerRequest;

extern std::atomic<bool> g_webAdminRebootRequested;
extern std::atomic<bool> g_webAdminWifiReconnectRequested;
extern std::atomic<bool> g_webAdminMqttApplyPending;
extern std::atomic<bool> g_webAdminSettingsApplyPending;
extern std::atomic<bool> g_webAdminChayaSendRequested;

extern uint8_t       g_webAdminPendingResetDays;
extern bool          g_webAdminPendingAuthEnabled;
extern portMUX_TYPE g_webAdminSettingsPendingMux;

bool adminParseBodyParam(AsyncWebServerRequest* req, const char* name, char* out,
                         size_t outLen);
