#include "admin_globals.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
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
bool         g_webAdminPendingLedEnabled   = true;
bool         g_webAdminPendingAudioMuted   = false;
uint8_t      g_webAdminPendingAudioVolume  = 70;
uint8_t      g_webAdminPendingQuiet0       = 23;
uint8_t      g_webAdminPendingQuiet1       = 8;
bool         g_webAdminPendingAudioCustom  = false;
uint16_t     g_webAdminPendingTxHz         = 95;
uint16_t     g_webAdminPendingTxMs         = 80;
uint16_t     g_webAdminPendingRxHz         = 88;
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
