#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes_api_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "identity/device_identity.h"
#include "heart/counter.h"
#include "battery/battery.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/csrf.h"
#include "web/deferred_reboot.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/test.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void handleApiSettingsGet(AsyncWebServerRequest* req) {
    char body[400];
    const int n = snprintf(
        body, sizeof(body),
        "{\"resetDays\":%u,\"lang\":\"%s\",\"theme\":\"%s\","
        "\"ledEnabled\":%s,\"audioMuted\":%s,\"audioVolume\":%u,\"quietHourStart\":%u,"
        "\"quietHourEnd\":%u,\"audioCustom\":%s,\"txHz\":%u,\"txMs\":%u,\"rxHz\":%u,\"rxMs\":%u}",
        static_cast<unsigned>(configGetResetPeriodDays()), configGetUiLang(), configGetUiTheme(),
        configGetLedEnabled() ? "true" : "false",
        configGetAudioMuted() ? "true" : "false", static_cast<unsigned>(configGetAudioVolume()),
        static_cast<unsigned>(configGetAudioQuietStart()),
        static_cast<unsigned>(configGetAudioQuietEnd()),
        configGetAudioCustom() ? "true" : "false", static_cast<unsigned>(configGetAudioTxHz()),
        static_cast<unsigned>(configGetAudioTxMs()), static_cast<unsigned>(configGetAudioRxHz()),
        static_cast<unsigned>(configGetAudioRxMs()));
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    webSendJson(req, 200, body);
}

void handleApiSettingsPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    uint8_t days = configGetResetPeriodDays();
    char lang[3];
    char theme[6];
    bool ledEnabled   = configGetLedEnabled();
    bool audioMuted   = configGetAudioMuted();
    uint8_t audioVol  = configGetAudioVolume();
    uint8_t quiet0    = configGetAudioQuietStart();
    uint8_t quiet1    = configGetAudioQuietEnd();
    bool audioCustom  = configGetAudioCustom();
    uint16_t txHz     = configGetAudioTxHz();
    uint16_t txMs     = configGetAudioTxMs();
    uint16_t rxHz     = configGetAudioRxHz();
    uint16_t rxMs     = configGetAudioRxMs();
    strlcpy(lang, configGetUiLang(), sizeof(lang));
    strlcpy(theme, configGetUiTheme(), sizeof(theme));

    if (req->hasParam("reset_days", true)) {
        const AsyncWebParameter* p = req->getParam("reset_days", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || v < 0 || v > 30) {
                sendErr(req, 400, "reset_days");
                return;
            }
            days = static_cast<uint8_t>(v);
        }
    }
    if (req->hasParam("lang", true)) {
        const AsyncWebParameter* p = req->getParam("lang", true);
        if (p != nullptr) {
            const String v = p->value();
            if (uiLangSyntaxOk(v.c_str())) {
                strlcpy(lang, v.c_str(), sizeof(lang));
            } else {
                sendErr(req, 400, "lang");
                return;
            }
        }
    }
    if (req->hasParam("theme", true)) {
        const AsyncWebParameter* p = req->getParam("theme", true);
        if (p != nullptr) {
            const String v = p->value();
            if (uiThemeSyntaxOk(v.c_str())) {
                strlcpy(theme, v.c_str(), sizeof(theme));
            } else {
                sendErr(req, 400, "theme");
                return;
            }
        }
    }
    if (req->hasParam("led_enabled", true)) {
        const AsyncWebParameter* p = req->getParam("led_enabled", true);
        if (p != nullptr) {
            const String v = p->value();
            if (!formBoolSyntaxOk(v.c_str())) {
                sendErr(req, 400, "led_enabled");
                return;
            }
            ledEnabled = formBoolFromForm(v.c_str());
        }
    }
    if (req->hasParam("audio_muted", true)) {
        const AsyncWebParameter* p = req->getParam("audio_muted", true);
        if (p != nullptr) {
            const String v = p->value();
            if (!formBoolSyntaxOk(v.c_str())) {
                sendErr(req, 400, "audio_muted");
                return;
            }
            audioMuted = formBoolFromForm(v.c_str());
        }
    }
    if (req->hasParam("audio_volume", true)) {
        const AsyncWebParameter* p = req->getParam("audio_volume", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || !audioVolumeInRange(v)) {
                sendErr(req, 400, "audio_volume");
                return;
            }
            audioVol = static_cast<uint8_t>(v);
        }
    }
    if (req->hasParam("quiet_hour_start", true)) {
        const AsyncWebParameter* p = req->getParam("quiet_hour_start", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || !quietHourInRange(v)) {
                sendErr(req, 400, "quiet_hour_start");
                return;
            }
            quiet0 = static_cast<uint8_t>(v);
        }
    }
    if (req->hasParam("quiet_hour_end", true)) {
        const AsyncWebParameter* p = req->getParam("quiet_hour_end", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || !quietHourInRange(v)) {
                sendErr(req, 400, "quiet_hour_end");
                return;
            }
            quiet1 = static_cast<uint8_t>(v);
        }
    }
    if (req->hasParam("audio_custom", true)) {
        const AsyncWebParameter* p = req->getParam("audio_custom", true);
        if (p != nullptr) {
            const String v = p->value();
            if (!formBoolSyntaxOk(v.c_str())) {
                sendErr(req, 400, "audio_custom");
                return;
            }
            audioCustom = formBoolFromForm(v.c_str());
        }
    }
    if (req->hasParam("tx_hz", true)) {
        const AsyncWebParameter* p = req->getParam("tx_hz", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || !audioToneHzInRange(v)) {
                sendErr(req, 400, "tx_hz");
                return;
            }
            txHz = static_cast<uint16_t>(v);
        }
    }
    if (req->hasParam("tx_ms", true)) {
        const AsyncWebParameter* p = req->getParam("tx_ms", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || !audioToneMsInRange(v)) {
                sendErr(req, 400, "tx_ms");
                return;
            }
            txMs = static_cast<uint16_t>(v);
        }
    }
    if (req->hasParam("rx_hz", true)) {
        const AsyncWebParameter* p = req->getParam("rx_hz", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || !audioToneHzInRange(v)) {
                sendErr(req, 400, "rx_hz");
                return;
            }
            rxHz = static_cast<uint16_t>(v);
        }
    }
    if (req->hasParam("rx_ms", true)) {
        const AsyncWebParameter* p = req->getParam("rx_ms", true);
        if (p != nullptr) {
            int v = 0;
            if (!parseFormIntStrict(p->value(), &v) || !audioToneMsInRange(v)) {
                sendErr(req, 400, "rx_ms");
                return;
            }
            rxMs = static_cast<uint16_t>(v);
        }
    }

    portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminPendingResetDays = days;
    strlcpy(g_webAdminPendingUiLang, lang, sizeof(g_webAdminPendingUiLang));
    strlcpy(g_webAdminPendingUiTheme, theme, sizeof(g_webAdminPendingUiTheme));
    g_webAdminPendingLedEnabled   = ledEnabled;
    g_webAdminPendingAudioMuted   = audioMuted;
    g_webAdminPendingAudioVolume  = audioVol;
    g_webAdminPendingQuiet0       = quiet0;
    g_webAdminPendingQuiet1       = quiet1;
    g_webAdminPendingAudioCustom  = audioCustom;
    g_webAdminPendingTxHz         = txHz;
    g_webAdminPendingTxMs         = txMs;
    g_webAdminPendingRxHz         = rxHz;
    g_webAdminPendingRxMs         = rxMs;
    portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminSettingsApplyPending.store(true, std::memory_order_release);
    sendOk(req, 200, "\"message\":\"saved\"");
}


void adminRoutesRegisterApiSettings(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h = ws.on("/api/settings", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiSettingsGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/settings", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiSettingsPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
