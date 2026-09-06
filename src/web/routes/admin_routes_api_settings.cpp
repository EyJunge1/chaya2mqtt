#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "async/app_task.h"
#include "battery/battery.h"
#include "battery/battery_pure.h"
#include "config/app_config.h"
#include "constants.h"
#include "web/web_utils.h"

#include <ESPAsyncWebServer.h>
#include <cstring>

void fillSettingsJson(JsonObject obj) {
    char lang[3]{};
    char theme[8]{};
    configCopyUiLang(lang, sizeof(lang));
    configCopyUiTheme(theme, sizeof(theme));
    obj["resetDays"] = configGetResetPeriodDays();
    obj["lang"] = lang;
    obj["theme"] = theme;
    obj["ledEnabled"] = configGetLedEnabled();
    obj["audioTxEnabled"] = configGetAudioTxEnabled();
    obj["audioRxEnabled"] = configGetAudioRxEnabled();
    obj["audioTxVolume"] = configGetAudioTxVolume();
    obj["audioRxVolume"] = configGetAudioRxVolume();
    obj["quietHourStart"] = configGetAudioQuietStart();
    obj["quietHourEnd"] = configGetAudioQuietEnd();
    obj["txHz"] = configGetAudioTxHz();
    obj["txMs"] = configGetAudioTxMs();
    obj["rxHz"] = configGetAudioRxHz();
    obj["rxMs"] = configGetAudioRxMs();
    obj["nvsOk"] = !g_webAdminSettingsNvsWriteFailed.load(std::memory_order_acquire);
    obj["applyPending"] = g_webAdminSettingsApplyPending.load(std::memory_order_acquire);
}

void handleApiSettingsGet(AsyncWebServerRequest *req) {
    JsonDocument doc;
    fillSettingsJson(doc.to<JsonObject>());
    webSendJsonDoc(req, 200, doc);
}

void handleApiSettingsPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (batteryCriticalLow(batteryPercent())) {
        sendErr(req, 503, "battery_low");
        return;
    }
    // RC-FE-01: overlay on pending while apply is in flight; live only when !applyPending.
    uint8_t days;
    char lang[3];
    char theme[8];
    bool ledEnabled;
    bool audioTxEnabled;
    bool audioRxEnabled;
    uint8_t audioTxVol;
    uint8_t audioRxVol;
    uint8_t quiet0;
    uint8_t quiet1;
    uint16_t txHz;
    uint16_t txMs;
    uint16_t rxHz;
    uint16_t rxMs;
    if (g_webAdminSettingsApplyPending.load(std::memory_order_acquire)) {
        portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
        days = g_webAdminPendingResetDays;
        strlcpy(lang, g_webAdminPendingUiLang, sizeof(lang));
        strlcpy(theme, g_webAdminPendingUiTheme, sizeof(theme));
        ledEnabled = g_webAdminPendingLedEnabled;
        audioTxEnabled = g_webAdminPendingAudioTxEnabled;
        audioRxEnabled = g_webAdminPendingAudioRxEnabled;
        audioTxVol = g_webAdminPendingAudioTxVolume;
        audioRxVol = g_webAdminPendingAudioRxVolume;
        quiet0 = g_webAdminPendingQuiet0;
        quiet1 = g_webAdminPendingQuiet1;
        txHz = g_webAdminPendingTxHz;
        txMs = g_webAdminPendingTxMs;
        rxHz = g_webAdminPendingRxHz;
        rxMs = g_webAdminPendingRxMs;
        portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
    } else {
        days = configGetResetPeriodDays();
        ledEnabled = configGetLedEnabled();
        audioTxEnabled = configGetAudioTxEnabled();
        audioRxEnabled = configGetAudioRxEnabled();
        audioTxVol = configGetAudioTxVolume();
        audioRxVol = configGetAudioRxVolume();
        quiet0 = configGetAudioQuietStart();
        quiet1 = configGetAudioQuietEnd();
        txHz = configGetAudioTxHz();
        txMs = configGetAudioTxMs();
        rxHz = configGetAudioRxHz();
        rxMs = configGetAudioRxMs();
        configCopyUiLang(lang, sizeof(lang));
        configCopyUiTheme(theme, sizeof(theme));
    }

    if (!adminApplyOptionalU8(json, "reset_days", resetPeriodDaysInRange, &days)) {
        sendErr(req, 400, "reset_days");
        return;
    }
    if (!adminApplyOptionalString(json, "lang", lang, sizeof(lang), uiLangSyntaxOk)) {
        sendErr(req, 400, "lang");
        return;
    }
    if (!adminApplyOptionalString(json, "theme", theme, sizeof(theme), uiThemeSyntaxOk)) {
        sendErr(req, 400, "theme");
        return;
    }
    if (!adminApplyOptionalBool(json, "led_enabled", &ledEnabled)) {
        sendErr(req, 400, "led_enabled");
        return;
    }
    if (!adminApplyOptionalBool(json, "audio_tx_enabled", &audioTxEnabled)) {
        sendErr(req, 400, "audio_tx_enabled");
        return;
    }
    if (!adminApplyOptionalBool(json, "audio_rx_enabled", &audioRxEnabled)) {
        sendErr(req, 400, "audio_rx_enabled");
        return;
    }
    if (!adminApplyOptionalU8(json, "audio_tx_volume", audioVolumeInRange, &audioTxVol)) {
        sendErr(req, 400, "audio_tx_volume");
        return;
    }
    if (!adminApplyOptionalU8(json, "audio_rx_volume", audioVolumeInRange, &audioRxVol)) {
        sendErr(req, 400, "audio_rx_volume");
        return;
    }
    if (!adminApplyOptionalU8(json, "quiet_hour_start", quietHourInRange, &quiet0)) {
        sendErr(req, 400, "quiet_hour_start");
        return;
    }
    if (!adminApplyOptionalU8(json, "quiet_hour_end", quietHourInRange, &quiet1)) {
        sendErr(req, 400, "quiet_hour_end");
        return;
    }
    if (!adminApplyOptionalU16(json, "tx_hz", audioToneHzInRange, &txHz)) {
        sendErr(req, 400, "tx_hz");
        return;
    }
    if (!adminApplyOptionalU16(json, "tx_ms", audioToneMsInRange, &txMs)) {
        sendErr(req, 400, "tx_ms");
        return;
    }
    if (!adminApplyOptionalU16(json, "rx_hz", audioToneHzInRange, &rxHz)) {
        sendErr(req, 400, "rx_hz");
        return;
    }
    if (!adminApplyOptionalU16(json, "rx_ms", audioToneMsInRange, &rxMs)) {
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
    g_webAdminSettingsApplyVersion.fetch_add(1U, std::memory_order_relaxed);
    portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminSettingsApplyPending.store(true, std::memory_order_release);
    // BUG-WEB-03: leave nvsOk until webAdminLoop records the apply result.
    appTaskNotify();
    sendOk(req, 200, "accepted");
}

void adminRoutesRegisterApiSettings(AsyncWebServer &ws) {
    adminOnGet(ws, "/api/settings", handleApiSettingsGet, ApiGuard::Sta);
    adminAddJsonPost(ws, "/api/settings", handleApiSettingsPost, ApiGuard::Sta);
}
