#include "admin_globals.h"

#include "constants.h"

#include <Arduino.h>
#include <climits>
#include <cstring>
#include <variant>

std::atomic<bool> g_webAdminRebootRequested{false};
std::atomic<bool> g_webAdminWifiReconnectRequested{false};
std::atomic<uint32_t> g_webAdminMqttApplyVersion{0};
std::atomic<uint32_t> g_webAdminSettingsApplyVersion{0};
std::atomic<bool> g_webAdminSettingsApplyPending{false};
std::atomic<bool> g_webAdminSettingsNvsWriteFailed{false};
std::atomic<uint32_t> g_webAdminApplyInFlight{0};

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

auto adminOptionalJsonInt(JsonVariantConst obj, const char *name) -> AdminJsonResult<int> {
    if (name == nullptr || !obj.is<JsonObjectConst>()) {
        return std::unexpected(AdminJsonError::Invalid);
    }
    const JsonVariantConst v = obj[name];
    if (v.isUnbound()) {
        return std::optional<int>{};
    }
    if (v.is<int>()) {
        return std::optional<int>{v.as<int>()};
    }
    if (v.is<unsigned int>()) {
        const unsigned int u = v.as<unsigned int>();
        if (u > static_cast<unsigned int>(INT_MAX)) {
            return std::unexpected(AdminJsonError::Invalid);
        }
        return std::optional<int>{static_cast<int>(u)};
    }
    return std::unexpected(AdminJsonError::Invalid);
}

auto adminOptionalJsonBool(JsonVariantConst obj, const char *name) -> AdminJsonResult<bool> {
    if (name == nullptr || !obj.is<JsonObjectConst>()) {
        return std::unexpected(AdminJsonError::Invalid);
    }
    const JsonVariantConst v = obj[name];
    if (v.isUnbound()) {
        return std::optional<bool>{};
    }
    if (!v.is<bool>()) {
        return std::unexpected(AdminJsonError::Invalid);
    }
    return std::optional<bool>{v.as<bool>()};
}

auto adminOptionalJsonString(JsonVariantConst obj, const char *name, char *out, size_t outLen)
    -> AdminJsonResult<std::monostate> {
    if (out == nullptr || outLen == 0U || name == nullptr || !obj.is<JsonObjectConst>()) {
        return std::unexpected(AdminJsonError::Invalid);
    }
    const JsonVariantConst v = obj[name];
    if (v.isUnbound()) {
        return std::optional<std::monostate>{};
    }
    if (!v.is<const char *>()) {
        return std::unexpected(AdminJsonError::Invalid);
    }
    const char *s = v.as<const char *>();
    if (s == nullptr || strlen(s) >= outLen) {
        return std::unexpected(AdminJsonError::Invalid);
    }
    strlcpy(out, s, outLen);
    return std::optional<std::monostate>{std::monostate{}};
}

bool adminApplyOptionalInt(JsonVariantConst obj, const char *name, bool (*inRange)(int), int *out) {
    if (out == nullptr) {
        return false;
    }
    const auto field = adminOptionalJsonInt(obj, name);
    if (!field.has_value()) {
        return false;
    }
    if (!field->has_value()) {
        return true;
    }
    if (inRange != nullptr && !inRange(**field)) {
        return false;
    }
    *out = **field;
    return true;
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
    const auto field = adminOptionalJsonBool(obj, name);
    if (!field.has_value()) {
        return false;
    }
    if (field->has_value()) {
        *out = **field;
    }
    return true;
}

bool adminApplyOptionalString(JsonVariantConst obj, const char *name, char *out, size_t outLen, bool (*syntaxOk)(const char *)) {
    if (out == nullptr || outLen == 0U) {
        return false;
    }
    const auto field = adminOptionalJsonString(obj, name, out, outLen);
    if (!field.has_value()) {
        return false;
    }
    if (!field->has_value()) {
        return true;
    }
    return syntaxOk == nullptr || syntaxOk(out);
}
