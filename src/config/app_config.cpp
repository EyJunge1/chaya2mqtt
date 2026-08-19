#include "app_config.h"

#include "nvs_utils.h"
#include "nvs_keys.h"
#include "audio/audio_config.h"
#include "constants.h"

#include <atomic>
#include <Arduino.h>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("CFG");

// NVS namespace "cfg" — cached: display reset period, UI language, UI theme, display dark.

static std::atomic<uint8_t> s_resetPeriodDaysCached{7};
static char s_uiLangCached[3] = "en";
static char s_uiThemeCached[6] = "light";
static std::atomic<bool> s_displayDarkCached{false};
static std::atomic<bool> s_audioMutedCached{false};
static std::atomic<uint8_t> s_audioVolumeCached{kAudioDefaultVolume};
static std::atomic<uint8_t> s_audioQuiet0Cached{kAudioDefaultQuiet0};
static std::atomic<uint8_t> s_audioQuiet1Cached{kAudioDefaultQuiet1};
static portMUX_TYPE s_uiPrefsMux = portMUX_INITIALIZER_UNLOCKED;

void configLoadResetPeriodFromNvs() {
    const uint8_t raw = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgRstPeriod, 7);
    if (raw == 0U) {
        s_resetPeriodDaysCached.store(0, std::memory_order_relaxed);
        return;
    }
    if (raw <= 30U) {
        s_resetPeriodDaysCached.store(raw, std::memory_order_relaxed);
        return;
    }
    s_resetPeriodDaysCached.store(7, std::memory_order_relaxed);
}

void configLoadUiPrefsFromNvs() {
    char lang[8] = {};
    char theme[8] = {};
    app_nvs::readString(kNvsNsCfg, kNvsKeyCfgUiLang, lang, sizeof(lang));
    app_nvs::readString(kNvsNsCfg, kNvsKeyCfgUiTheme, theme, sizeof(theme));

    portENTER_CRITICAL(&s_uiPrefsMux);
    if (uiLangSyntaxOk(lang)) {
        strlcpy(s_uiLangCached, lang, sizeof(s_uiLangCached));
    } else {
        strlcpy(s_uiLangCached, "en", sizeof(s_uiLangCached));
    }
    if (uiThemeSyntaxOk(theme)) {
        strlcpy(s_uiThemeCached, theme, sizeof(s_uiThemeCached));
    } else {
        strlcpy(s_uiThemeCached, "light", sizeof(s_uiThemeCached));
    }
    portEXIT_CRITICAL(&s_uiPrefsMux);
}

uint8_t configGetResetPeriodDays() {
    return s_resetPeriodDaysCached.load(std::memory_order_relaxed);
}

bool configSetResetPeriodDays(uint8_t days) {
    if (days > 30U) {
        days = 30U;
    }
    if (configGetResetPeriodDays() == days) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgRstPeriod, days)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist rstPeriod");
        return false;
    }
    s_resetPeriodDaysCached.store(days, std::memory_order_relaxed);
    return true;
}

const char* configGetUiLang() {
    return s_uiLangCached;
}

bool configSetUiLang(const char* lang) {
    if (!uiLangSyntaxOk(lang)) {
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    const bool same = strcmp(s_uiLangCached, lang) == 0;
    portEXIT_CRITICAL(&s_uiPrefsMux);
    if (same) {
        return true;
    }
    if (!app_nvs::writeString(kNvsNsCfg, kNvsKeyCfgUiLang, lang)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist ui_lang");
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    strlcpy(s_uiLangCached, lang, sizeof(s_uiLangCached));
    portEXIT_CRITICAL(&s_uiPrefsMux);
    return true;
}

const char* configGetUiTheme() {
    return s_uiThemeCached;
}

bool configSetUiTheme(const char* theme) {
    if (!uiThemeSyntaxOk(theme)) {
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    const bool same = strcmp(s_uiThemeCached, theme) == 0;
    portEXIT_CRITICAL(&s_uiPrefsMux);
    if (same) {
        return true;
    }
    if (!app_nvs::writeString(kNvsNsCfg, kNvsKeyCfgUiTheme, theme)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist ui_theme");
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    strlcpy(s_uiThemeCached, theme, sizeof(s_uiThemeCached));
    portEXIT_CRITICAL(&s_uiPrefsMux);
    return true;
}

void configLoadDisplayDarkFromNvs() {
    const uint8_t raw = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgDispDark, 0);
    s_displayDarkCached.store(raw != 0U, std::memory_order_relaxed);
}

bool configGetDisplayDark() {
    return s_displayDarkCached.load(std::memory_order_relaxed);
}

bool configSetDisplayDark(bool dark) {
    if (configGetDisplayDark() == dark) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgDispDark, dark ? 1U : 0U)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist disp_dark");
        return false;
    }
    s_displayDarkCached.store(dark, std::memory_order_relaxed);
    return true;
}

void configLoadAudioFromNvs() {
    const uint8_t mute = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndMute, 0);
    uint8_t vol        = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndVol, kAudioDefaultVolume);
    uint8_t q0         = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndQ0, kAudioDefaultQuiet0);
    uint8_t q1         = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndQ1, kAudioDefaultQuiet1);
    if (vol > kAudioVolumeMax) {
        vol = kAudioDefaultVolume;
    }
    if (q0 > kAudioHourMax) {
        q0 = kAudioDefaultQuiet0;
    }
    if (q1 > kAudioHourMax) {
        q1 = kAudioDefaultQuiet1;
    }
    s_audioMutedCached.store(mute != 0U, std::memory_order_relaxed);
    s_audioVolumeCached.store(vol, std::memory_order_relaxed);
    s_audioQuiet0Cached.store(q0, std::memory_order_relaxed);
    s_audioQuiet1Cached.store(q1, std::memory_order_relaxed);
}

bool configGetAudioMuted() {
    return s_audioMutedCached.load(std::memory_order_relaxed);
}

bool configSetAudioMuted(bool muted) {
    if (configGetAudioMuted() == muted) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndMute, muted ? 1U : 0U)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist snd_mute");
        return false;
    }
    s_audioMutedCached.store(muted, std::memory_order_relaxed);
    return true;
}

uint8_t configGetAudioVolume() {
    return s_audioVolumeCached.load(std::memory_order_relaxed);
}

bool configSetAudioVolume(uint8_t volume) {
    if (volume > kAudioVolumeMax) {
        volume = kAudioVolumeMax;
    }
    if (configGetAudioVolume() == volume) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndVol, volume)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist snd_vol");
        return false;
    }
    s_audioVolumeCached.store(volume, std::memory_order_relaxed);
    return true;
}

uint8_t configGetAudioQuietStart() {
    return s_audioQuiet0Cached.load(std::memory_order_relaxed);
}

uint8_t configGetAudioQuietEnd() {
    return s_audioQuiet1Cached.load(std::memory_order_relaxed);
}

bool configSetAudioQuietHours(uint8_t startHour, uint8_t endHour) {
    if (startHour > kAudioHourMax) {
        startHour = kAudioHourMax;
    }
    if (endHour > kAudioHourMax) {
        endHour = kAudioHourMax;
    }
    if (configGetAudioQuietStart() == startHour && configGetAudioQuietEnd() == endHour) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndQ0, startHour)
        || !app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndQ1, endHour)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist snd_q0/snd_q1");
        return false;
    }
    s_audioQuiet0Cached.store(startHour, std::memory_order_relaxed);
    s_audioQuiet1Cached.store(endHour, std::memory_order_relaxed);
    return true;
}

void app_configResetRamAfterFactoryClear() {
    s_resetPeriodDaysCached.store(7, std::memory_order_relaxed);
    s_displayDarkCached.store(false, std::memory_order_relaxed);
    s_audioMutedCached.store(false, std::memory_order_relaxed);
    s_audioVolumeCached.store(kAudioDefaultVolume, std::memory_order_relaxed);
    s_audioQuiet0Cached.store(kAudioDefaultQuiet0, std::memory_order_relaxed);
    s_audioQuiet1Cached.store(kAudioDefaultQuiet1, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_uiPrefsMux);
    strlcpy(s_uiLangCached, "en", sizeof(s_uiLangCached));
    strlcpy(s_uiThemeCached, "light", sizeof(s_uiThemeCached));
    portEXIT_CRITICAL(&s_uiPrefsMux);
}
