#include "admin_globals.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstring>

std::atomic<bool> g_webAdminRebootRequested{false};
std::atomic<bool> g_webAdminWifiReconnectRequested{false};
std::atomic<bool> g_webAdminMqttApplyPending{false};
std::atomic<bool> g_webAdminSettingsApplyPending{false};
std::atomic<bool> g_webAdminMqttNvsWriteFailed{false};
std::atomic<bool> g_webAdminSettingsNvsWriteFailed{false};
std::atomic<bool> g_systemShutdownInProgress{false};

uint8_t       g_webAdminPendingResetDays     = 7;
bool          g_webAdminPendingAuthEnabled   = false;
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
