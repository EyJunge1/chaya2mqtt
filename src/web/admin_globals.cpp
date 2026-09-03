#include "admin_globals.h"

#include "constants.h"

#include <Arduino.h>
#include <cerrno>
#include <climits>
#include <cstring>

std::atomic<bool> g_webAdminRebootRequested{false};
std::atomic<bool> g_webAdminWifiReconnectRequested{false};
std::atomic<uint32_t> g_webAdminMqttApplyVersion{0};
std::atomic<bool> g_webAdminSettingsApplyPending{false};
std::atomic<bool> g_webAdminSettingsNvsWriteFailed{false};

uint8_t g_webAdminPendingResetDays = 7;
char g_webAdminPendingUiLang[3] = "en";
char g_webAdminPendingUiTheme[8] = "system";
bool g_webAdminPendingLedEnabled = true;
bool g_webAdminPendingAudioTxEnabled = false;
bool g_webAdminPendingAudioRxEnabled = false;
uint8_t g_webAdminPendingAudioTxVolume = 70;
uint8_t g_webAdminPendingAudioRxVolume = 70;
uint8_t g_webAdminPendingQuiet0 = 0;
uint8_t g_webAdminPendingQuiet1 = 0;
uint16_t g_webAdminPendingTxHz = 880;
uint16_t g_webAdminPendingTxMs = 80;
uint16_t g_webAdminPendingRxHz = 660;
uint16_t g_webAdminPendingRxMs = 140;
portMUX_TYPE g_webAdminSettingsPendingMux = portMUX_INITIALIZER_UNLOCKED;

bool adminJsonHasField(JsonVariantConst obj, const char *name) {
    return name != nullptr && obj.is<JsonObjectConst>() && !obj[name].isUnbound();
}

AdminJsonParam adminOptionalJsonInt(JsonVariantConst obj, const char *name, int *out) {
    if (out == nullptr || name == nullptr || !obj.is<JsonObjectConst>()) {
        return AdminJsonParam::Invalid;
    }
    const JsonVariantConst v = obj[name];
    if (v.isUnbound()) {
        return AdminJsonParam::Absent;
    }
    if (v.is<int>()) {
        *out = v.as<int>();
        return AdminJsonParam::Ok;
    }
    if (v.is<unsigned int>()) {
        const unsigned int u = v.as<unsigned int>();
        if (u > static_cast<unsigned int>(INT_MAX)) {
            return AdminJsonParam::Invalid;
        }
        *out = static_cast<int>(u);
        return AdminJsonParam::Ok;
    }
    return AdminJsonParam::Invalid;
}

AdminJsonParam adminOptionalJsonBool(JsonVariantConst obj, const char *name, bool *out) {
    if (out == nullptr || name == nullptr || !obj.is<JsonObjectConst>()) {
        return AdminJsonParam::Invalid;
    }
    const JsonVariantConst v = obj[name];
    if (v.isUnbound()) {
        return AdminJsonParam::Absent;
    }
    if (!v.is<bool>()) {
        return AdminJsonParam::Invalid;
    }
    *out = v.as<bool>();
    return AdminJsonParam::Ok;
}

AdminJsonParam adminOptionalJsonString(JsonVariantConst obj, const char *name, char *out, size_t outLen) {
    if (out == nullptr || outLen == 0U || name == nullptr || !obj.is<JsonObjectConst>()) {
        return AdminJsonParam::Invalid;
    }
    const JsonVariantConst v = obj[name];
    if (v.isUnbound()) {
        return AdminJsonParam::Absent;
    }
    if (!v.is<const char *>()) {
        return AdminJsonParam::Invalid;
    }
    const char *s = v.as<const char *>();
    if (s == nullptr || strlen(s) >= outLen) {
        return AdminJsonParam::Invalid;
    }
    strlcpy(out, s, outLen);
    return AdminJsonParam::Ok;
}

bool adminApplyOptionalInt(JsonVariantConst obj, const char *name, bool (*inRange)(int), int *out) {
    if (out == nullptr) {
        return false;
    }
    int v = 0;
    switch (adminOptionalJsonInt(obj, name, &v)) {
    case AdminJsonParam::Absent:
        return true;
    case AdminJsonParam::Invalid:
        return false;
    case AdminJsonParam::Ok:
        if (inRange != nullptr && !inRange(v)) {
            return false;
        }
        *out = v;
        return true;
    }
    return false;
}

bool adminApplyOptionalU8(JsonVariantConst obj, const char *name, bool (*inRange)(int), uint8_t *out) {
    if (out == nullptr) {
        return false;
    }
    int v = static_cast<int>(*out);
    if (!adminApplyOptionalInt(obj, name, inRange, &v)) {
        return false;
    }
    *out = static_cast<uint8_t>(v);
    return true;
}

bool adminApplyOptionalU16(JsonVariantConst obj, const char *name, bool (*inRange)(int), uint16_t *out) {
    if (out == nullptr) {
        return false;
    }
    int v = static_cast<int>(*out);
    if (!adminApplyOptionalInt(obj, name, inRange, &v)) {
        return false;
    }
    *out = static_cast<uint16_t>(v);
    return true;
}

bool adminApplyOptionalBool(JsonVariantConst obj, const char *name, bool *out) {
    if (out == nullptr) {
        return false;
    }
    switch (adminOptionalJsonBool(obj, name, out)) {
    case AdminJsonParam::Absent:
        return true;
    case AdminJsonParam::Invalid:
        return false;
    case AdminJsonParam::Ok:
        return true;
    }
    return false;
}

bool adminApplyOptionalString(JsonVariantConst obj, const char *name, char *out, size_t outLen, bool (*syntaxOk)(const char *)) {
    if (out == nullptr || outLen == 0U) {
        return false;
    }
    switch (adminOptionalJsonString(obj, name, out, outLen)) {
    case AdminJsonParam::Absent:
        return true;
    case AdminJsonParam::Invalid:
        return false;
    case AdminJsonParam::Ok:
        if (syntaxOk != nullptr && !syntaxOk(out)) {
            return false;
        }
        return true;
    }
    return false;
}
