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

uint8_t      g_webAdminPendingResetDays   = 7;
char         g_webAdminPendingUiLang[3]   = "en";
char         g_webAdminPendingUiTheme[6]  = "light";
portMUX_TYPE g_webAdminSettingsPendingMux = portMUX_INITIALIZER_UNLOCKED;

bool adminParseBodyParam(AsyncWebServerRequest* req, const char* name, char* out,
                         size_t outLen) {
    if (req == nullptr || !req->hasParam(name, true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam(name, true);
    if (p == nullptr) {
        return false;
    }
    strlcpy(out, p->value().c_str(), outLen);
    return true;
}
