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
extern bool         g_webAdminPendingLedEnabled;
extern bool         g_webAdminPendingAudioMuted;
extern uint8_t      g_webAdminPendingAudioVolume;
extern uint8_t      g_webAdminPendingQuiet0;
extern uint8_t      g_webAdminPendingQuiet1;
extern bool         g_webAdminPendingAudioCustom;
extern uint16_t     g_webAdminPendingTxHz;
extern uint16_t     g_webAdminPendingTxMs;
extern uint16_t     g_webAdminPendingRxHz;
extern uint16_t     g_webAdminPendingRxMs;
extern portMUX_TYPE g_webAdminSettingsPendingMux;

bool adminParseBodyParam(AsyncWebServerRequest* req, const char* name, char* out,
                         size_t outLen);

/** Optional POST body fields: absent vs. present-and-ok vs. present-but-invalid. */
enum class AdminFormParam : uint8_t { Absent, Ok, Invalid };

AdminFormParam adminOptionalFormInt(AsyncWebServerRequest* req, const char* name, int* out);
AdminFormParam adminOptionalFormBool(AsyncWebServerRequest* req, const char* name, bool* out);
/** Absent if missing; Ok if copied; Invalid if present but too long for outLen. */
AdminFormParam adminOptionalFormString(AsyncWebServerRequest* req, const char* name, char* out,
                                       size_t outLen);
