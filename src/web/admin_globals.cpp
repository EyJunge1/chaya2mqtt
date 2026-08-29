#include "admin_globals.h"

#include "constants.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

std::atomic<bool> g_webAdminRebootRequested{false};
std::atomic<bool> g_webAdminWifiReconnectRequested{false};
std::atomic<uint32_t> g_webAdminMqttApplyVersion{0};
std::atomic<bool> g_webAdminSettingsApplyPending{false};
std::atomic<bool> g_webAdminMqttNvsWriteFailed{false};
std::atomic<bool> g_webAdminSettingsNvsWriteFailed{false};
std::atomic<bool> g_systemShutdownInProgress{false};

uint8_t      g_webAdminPendingResetDays    = 7;
char         g_webAdminPendingUiLang[3]    = "en";
char         g_webAdminPendingUiTheme[6]   = "light";
bool         g_webAdminPendingLedEnabled       = true;
bool         g_webAdminPendingAudioTxEnabled   = false;
bool         g_webAdminPendingAudioRxEnabled   = false;
uint8_t      g_webAdminPendingAudioTxVolume    = 70;
uint8_t      g_webAdminPendingAudioRxVolume    = 70;
uint8_t      g_webAdminPendingQuiet0       = 0;
uint8_t      g_webAdminPendingQuiet1       = 0;
uint16_t     g_webAdminPendingTxHz         = 880;
uint16_t     g_webAdminPendingTxMs         = 80;
uint16_t     g_webAdminPendingRxHz         = 660;
uint16_t     g_webAdminPendingRxMs         = 140;
portMUX_TYPE g_webAdminSettingsPendingMux  = portMUX_INITIALIZER_UNLOCKED;

bool adminParseBodyParam(AsyncWebServerRequest* req, const char* name, char* out,
                         size_t outLen) {
    if (req == nullptr || name == nullptr || out == nullptr || outLen == 0U
        || !req->hasParam(name, true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam(name, true);
    if (p == nullptr || p->value().length() >= outLen) {
        out[0] = '\0';
        return false;
    }
    strlcpy(out, p->value().c_str(), outLen);
    return true;
}

AdminFormParam adminOptionalFormInt(AsyncWebServerRequest* req, const char* name, int* out) {
    if (req == nullptr || name == nullptr || out == nullptr || !req->hasParam(name, true)) {
        return AdminFormParam::Absent;
    }
    const AsyncWebParameter* p = req->getParam(name, true);
    if (p == nullptr || p->value().length() == 0U) {
        return AdminFormParam::Invalid;
    }
    errno        = 0;
    char* end    = nullptr;
    const long v = strtol(p->value().c_str(), &end, 10);
    if (errno == ERANGE || end == p->value().c_str() || *end != '\0' || v < INT_MIN
        || v > INT_MAX) {
        return AdminFormParam::Invalid;
    }
    *out = static_cast<int>(v);
    return AdminFormParam::Ok;
}

AdminFormParam adminOptionalFormBool(AsyncWebServerRequest* req, const char* name, bool* out) {
    if (req == nullptr || name == nullptr || out == nullptr || !req->hasParam(name, true)) {
        return AdminFormParam::Absent;
    }
    const AsyncWebParameter* p = req->getParam(name, true);
    if (p == nullptr || !formBoolSyntaxOk(p->value().c_str())) {
        return AdminFormParam::Invalid;
    }
    *out = formBoolFromForm(p->value().c_str());
    return AdminFormParam::Ok;
}

AdminFormParam adminOptionalFormString(AsyncWebServerRequest* req, const char* name, char* out,
                                       size_t outLen) {
    if (req == nullptr || name == nullptr || out == nullptr || outLen == 0U
        || !req->hasParam(name, true)) {
        return AdminFormParam::Absent;
    }
    const AsyncWebParameter* p = req->getParam(name, true);
    if (p == nullptr || p->value().length() >= outLen) {
        return AdminFormParam::Invalid;
    }
    strlcpy(out, p->value().c_str(), outLen);
    return AdminFormParam::Ok;
}

bool adminApplyOptionalInt(AsyncWebServerRequest* req, const char* name, bool (*inRange)(int),
                           int* out) {
    if (out == nullptr) {
        return false;
    }
    int v = 0;
    switch (adminOptionalFormInt(req, name, &v)) {
    case AdminFormParam::Absent:
        return true;
    case AdminFormParam::Invalid:
        return false;
    case AdminFormParam::Ok:
        if (inRange != nullptr && !inRange(v)) {
            return false;
        }
        *out = v;
        return true;
    }
    return false;
}

bool adminApplyOptionalU8(AsyncWebServerRequest* req, const char* name, bool (*inRange)(int),
                          uint8_t* out) {
    if (out == nullptr) {
        return false;
    }
    int v = static_cast<int>(*out);
    if (!adminApplyOptionalInt(req, name, inRange, &v)) {
        return false;
    }
    *out = static_cast<uint8_t>(v);
    return true;
}

bool adminApplyOptionalU16(AsyncWebServerRequest* req, const char* name, bool (*inRange)(int),
                           uint16_t* out) {
    if (out == nullptr) {
        return false;
    }
    int v = static_cast<int>(*out);
    if (!adminApplyOptionalInt(req, name, inRange, &v)) {
        return false;
    }
    *out = static_cast<uint16_t>(v);
    return true;
}

bool adminApplyOptionalBool(AsyncWebServerRequest* req, const char* name, bool* out) {
    if (out == nullptr) {
        return false;
    }
    switch (adminOptionalFormBool(req, name, out)) {
    case AdminFormParam::Absent:
        return true;
    case AdminFormParam::Invalid:
        return false;
    case AdminFormParam::Ok:
        return true;
    }
    return false;
}

bool adminApplyOptionalString(AsyncWebServerRequest* req, const char* name, char* out,
                              size_t outLen, bool (*syntaxOk)(const char*)) {
    if (out == nullptr || outLen == 0U) {
        return false;
    }
    switch (adminOptionalFormString(req, name, out, outLen)) {
    case AdminFormParam::Absent:
        return true;
    case AdminFormParam::Invalid:
        return false;
    case AdminFormParam::Ok:
        if (syntaxOk != nullptr && !syntaxOk(out)) {
            return false;
        }
        return true;
    }
    return false;
}
