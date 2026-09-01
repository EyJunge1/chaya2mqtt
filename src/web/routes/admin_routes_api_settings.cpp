#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes_api_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "battery/battery.h"
#include "battery/battery_pure.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "heart/counter.h"
#include "identity/device_identity.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/csrf.h"
#include "web/deferred_reboot.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"

#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void handleApiSettingsGet(AsyncWebServerRequest *req) {
    char lang[3]{};
    char theme[8]{};
    configCopyUiLang(lang, sizeof(lang));
    configCopyUiTheme(theme, sizeof(theme));
    char body[512];
    const int n =
        snprintf(body, sizeof(body),
                 "{\"resetDays\":%u,\"lang\":\"%s\",\"theme\":\"%s\","
                 "\"ledEnabled\":%s,\"audioTxEnabled\":%s,\"audioRxEnabled\":%s,"
                 "\"audioTxVolume\":%u,\"audioRxVolume\":%u,\"quietHourStart\":%u,\"quietHourEnd\":%u,"
                 "\"txHz\":%u,\"txMs\":%u,\"rxHz\":%u,\"rxMs\":%u,"
                 "\"nvsOk\":%s,\"applyPending\":%s}",
                 static_cast<unsigned>(configGetResetPeriodDays()), lang, theme, configGetLedEnabled() ? "true" : "false",
                 configGetAudioTxEnabled() ? "true" : "false", configGetAudioRxEnabled() ? "true" : "false",
                 static_cast<unsigned>(configGetAudioTxVolume()), static_cast<unsigned>(configGetAudioRxVolume()),
                 static_cast<unsigned>(configGetAudioQuietStart()), static_cast<unsigned>(configGetAudioQuietEnd()),
                 static_cast<unsigned>(configGetAudioTxHz()), static_cast<unsigned>(configGetAudioTxMs()),
                 static_cast<unsigned>(configGetAudioRxHz()), static_cast<unsigned>(configGetAudioRxMs()),
                 g_webAdminSettingsNvsWriteFailed.load(std::memory_order_acquire) ? "false" : "true",
                 g_webAdminSettingsApplyPending.load(std::memory_order_acquire) ? "true" : "false");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        sendErr(req, 500, "json");
        return;
    }
    webSendJson(req, 200, body);
}

void handleApiSettingsPost(AsyncWebServerRequest *req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (batteryCriticalLow(batteryPercent())) {
        sendErr(req, 503, "battery_low");
        return;
    }
    uint8_t days = configGetResetPeriodDays();
    char lang[3];
    char theme[8];
    bool ledEnabled = configGetLedEnabled();
    bool audioTxEnabled = configGetAudioTxEnabled();
    bool audioRxEnabled = configGetAudioRxEnabled();
    uint8_t audioTxVol = configGetAudioTxVolume();
    uint8_t audioRxVol = configGetAudioRxVolume();
    uint8_t quiet0 = configGetAudioQuietStart();
    uint8_t quiet1 = configGetAudioQuietEnd();
    uint16_t txHz = configGetAudioTxHz();
    uint16_t txMs = configGetAudioTxMs();
    uint16_t rxHz = configGetAudioRxHz();
    uint16_t rxMs = configGetAudioRxMs();
    configCopyUiLang(lang, sizeof(lang));
    configCopyUiTheme(theme, sizeof(theme));

    if (!adminApplyOptionalU8(req, "reset_days", resetPeriodDaysInRange, &days)) {
        sendErr(req, 400, "reset_days");
        return;
    }
    if (!adminApplyOptionalString(req, "lang", lang, sizeof(lang), uiLangSyntaxOk)) {
        sendErr(req, 400, "lang");
        return;
    }
    if (!adminApplyOptionalString(req, "theme", theme, sizeof(theme), uiThemeSyntaxOk)) {
        sendErr(req, 400, "theme");
        return;
    }
    if (!adminApplyOptionalBool(req, "led_enabled", &ledEnabled)) {
        sendErr(req, 400, "led_enabled");
        return;
    }
    if (!adminApplyOptionalBool(req, "audio_tx_enabled", &audioTxEnabled)) {
        sendErr(req, 400, "audio_tx_enabled");
        return;
    }
    if (!adminApplyOptionalBool(req, "audio_rx_enabled", &audioRxEnabled)) {
        sendErr(req, 400, "audio_rx_enabled");
        return;
    }
    if (!adminApplyOptionalU8(req, "audio_tx_volume", audioVolumeInRange, &audioTxVol)) {
        sendErr(req, 400, "audio_tx_volume");
        return;
    }
    if (!adminApplyOptionalU8(req, "audio_rx_volume", audioVolumeInRange, &audioRxVol)) {
        sendErr(req, 400, "audio_rx_volume");
        return;
    }
    if (!adminApplyOptionalU8(req, "quiet_hour_start", quietHourInRange, &quiet0)) {
        sendErr(req, 400, "quiet_hour_start");
        return;
    }
    if (!adminApplyOptionalU8(req, "quiet_hour_end", quietHourInRange, &quiet1)) {
        sendErr(req, 400, "quiet_hour_end");
        return;
    }
    if (!adminApplyOptionalU16(req, "tx_hz", audioToneHzInRange, &txHz)) {
        sendErr(req, 400, "tx_hz");
        return;
    }
    if (!adminApplyOptionalU16(req, "tx_ms", audioToneMsInRange, &txMs)) {
        sendErr(req, 400, "tx_ms");
        return;
    }
    if (!adminApplyOptionalU16(req, "rx_hz", audioToneHzInRange, &rxHz)) {
        sendErr(req, 400, "rx_hz");
        return;
    }
    if (!adminApplyOptionalU16(req, "rx_ms", audioToneMsInRange, &rxMs)) {
        sendErr(req, 400, "rx_ms");
        return;
    }

    portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminPendingResetDays = days;
    strlcpy(g_webAdminPendingUiLang, lang, sizeof(g_webAdminPendingUiLang));
    strlcpy(g_webAdminPendingUiTheme, theme, sizeof(g_webAdminPendingUiTheme));
    g_webAdminPendingLedEnabled = ledEnabled;
    g_webAdminPendingAudioTxEnabled = audioTxEnabled;
    g_webAdminPendingAudioRxEnabled = audioRxEnabled;
    g_webAdminPendingAudioTxVolume = audioTxVol;
    g_webAdminPendingAudioRxVolume = audioRxVol;
    g_webAdminPendingQuiet0 = quiet0;
    g_webAdminPendingQuiet1 = quiet1;
    g_webAdminPendingTxHz = txHz;
    g_webAdminPendingTxMs = txMs;
    g_webAdminPendingRxHz = rxHz;
    g_webAdminPendingRxMs = rxMs;
    portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminSettingsApplyPending.store(true, std::memory_order_release);
    g_webAdminSettingsNvsWriteFailed.store(false, std::memory_order_release);
    sendOk(req, 200, "\"message\":\"accepted\"");
}

void adminRoutesRegisterApiSettings(AsyncWebServer &ws) {
    {
        AsyncCallbackWebHandler &h =
            ws.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiSettingsGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackWebHandler &h =
            ws.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *rq) { handleApiSettingsPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
