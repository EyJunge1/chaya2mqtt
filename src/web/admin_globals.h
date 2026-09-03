#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "async/system_lifecycle.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

extern std::atomic<bool> g_webAdminRebootRequested;
extern std::atomic<bool> g_webAdminWifiReconnectRequested;
extern std::atomic<uint32_t> g_webAdminMqttApplyVersion;
extern std::atomic<bool> g_webAdminSettingsApplyPending;
extern std::atomic<bool> g_webAdminSettingsNvsWriteFailed;

extern uint8_t g_webAdminPendingResetDays;
extern char g_webAdminPendingUiLang[3];
extern char g_webAdminPendingUiTheme[8];
extern bool g_webAdminPendingLedEnabled;
extern bool g_webAdminPendingAudioTxEnabled;
extern bool g_webAdminPendingAudioRxEnabled;
extern uint8_t g_webAdminPendingAudioTxVolume;
extern uint8_t g_webAdminPendingAudioRxVolume;
extern uint8_t g_webAdminPendingQuiet0;
extern uint8_t g_webAdminPendingQuiet1;
extern uint16_t g_webAdminPendingTxHz;
extern uint16_t g_webAdminPendingTxMs;
extern uint16_t g_webAdminPendingRxHz;
extern uint16_t g_webAdminPendingRxMs;
extern portMUX_TYPE g_webAdminSettingsPendingMux;

/** Optional JSON fields: absent vs. present-and-ok vs. present-but-invalid. */
enum class AdminJsonParam : uint8_t { Absent, Ok, Invalid };

bool adminJsonHasField(JsonVariantConst obj, const char *name);

AdminJsonParam adminOptionalJsonInt(JsonVariantConst obj, const char *name, int *out);
AdminJsonParam adminOptionalJsonBool(JsonVariantConst obj, const char *name, bool *out);
/** Absent if missing; Ok if copied; Invalid if present but not a fitting string. */
AdminJsonParam adminOptionalJsonString(JsonVariantConst obj, const char *name, char *out, size_t outLen);

/**
 * Apply optional JSON fields (Absent leaves *out unchanged).
 * Returns false when the field is present but invalid or fails predicate — caller sendErr(name).
 */
bool adminApplyOptionalInt(JsonVariantConst obj, const char *name, bool (*inRange)(int), int *out);
bool adminApplyOptionalU8(JsonVariantConst obj, const char *name, bool (*inRange)(int), uint8_t *out);
bool adminApplyOptionalU16(JsonVariantConst obj, const char *name, bool (*inRange)(int), uint16_t *out);
bool adminApplyOptionalBool(JsonVariantConst obj, const char *name, bool *out);
/** syntaxOk may be nullptr (any fitting string accepted). */
bool adminApplyOptionalString(JsonVariantConst obj, const char *name, char *out, size_t outLen,
                              bool (*syntaxOk)(const char *) = nullptr);
