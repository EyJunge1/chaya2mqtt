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

// NVS namespace "cfg" — cached: display reset period/view, UI language/theme, LED, audio.

struct AudioTone {
    uint16_t hz;
    uint16_t ms;
};

static std::atomic<uint8_t> s_resetPeriodDaysCached{7};
static char s_uiLangCached[3] = "en";
static char s_uiThemeCached[6] = "light";
static std::atomic<bool> s_ledEnabledCached{true};
static std::atomic<DisplayView> s_displayViewCached{DisplayView::Unknown};
static std::atomic<bool> s_audioTxEnabledCached{false};
static std::atomic<bool> s_audioRxEnabledCached{false};
static std::atomic<uint8_t> s_audioTxVolumeCached{kAudioDefaultVolume};
static std::atomic<uint8_t> s_audioRxVolumeCached{kAudioDefaultVolume};
static std::atomic<uint8_t> s_audioQuiet0Cached{kAudioDefaultQuiet0};
static std::atomic<uint8_t> s_audioQuiet1Cached{kAudioDefaultQuiet1};
static std::atomic<uint16_t> s_audioTxHzCached{kAudioDefaultTxHz};
static std::atomic<uint16_t> s_audioTxMsCached{kAudioDefaultTxMs};
static std::atomic<uint16_t> s_audioRxHzCached{kAudioDefaultRxHz};
static std::atomic<uint16_t> s_audioRxMsCached{kAudioDefaultRxMs};
static portMUX_TYPE s_uiPrefsMux = portMUX_INITIALIZER_UNLOCKED;

template <typename T>
static bool setCachedUChar(std::atomic<T>& cache, T value, const char* key, const char* errMsg) {
    if (cache.load(std::memory_order_relaxed) == value) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, key, static_cast<uint8_t>(value))) {
        ESP_LOGE(TAG, "%s", errMsg);
        return false;
    }
    cache.store(value, std::memory_order_relaxed);
    return true;
}

static bool setCachedBoolAsUChar(std::atomic<bool>& cache, bool value, const char* key,
                                 const char* errMsg) {
    if (cache.load(std::memory_order_relaxed) == value) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, key, value ? 1U : 0U)) {
        ESP_LOGE(TAG, "%s", errMsg);
        return false;
    }
    cache.store(value, std::memory_order_relaxed);
    return true;
}

static bool setCachedString(char* cache, size_t cacheLen, portMUX_TYPE* mux, const char* key,
                            const char* value, const char* errMsg) {
    if (cache == nullptr || mux == nullptr || key == nullptr || value == nullptr
        || cacheLen == 0U) {
        return false;
    }
    portENTER_CRITICAL(mux);
    const bool same = strcmp(cache, value) == 0;
    portEXIT_CRITICAL(mux);
    if (same) {
        return true;
    }
    if (!app_nvs::writeString(kNvsNsCfg, key, value)) {
        ESP_LOGE(TAG, "%s", errMsg);
        return false;
    }
    portENTER_CRITICAL(mux);
    strlcpy(cache, value, cacheLen);
    portEXIT_CRITICAL(mux);
    return true;
}

static uint16_t clampAudioToneHz(uint32_t raw, uint16_t fallback) {
    if (raw < kAudioToneHzMin || raw > kAudioToneHzMax) {
        return fallback;
    }
    return static_cast<uint16_t>(raw);
}

static uint16_t clampAudioToneMs(uint32_t raw, uint16_t fallback) {
    if (raw < kAudioToneMsMin || raw > kAudioToneMsMax) {
        return fallback;
    }
    return static_cast<uint16_t>(raw);
}

void configLoadResetPeriodFromNvs() {
    const uint8_t raw = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgRstPeriod, 7);
    if (resetPeriodDaysInRange(static_cast<int>(raw))) {
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
    return setCachedUChar(s_resetPeriodDaysCached, days, kNvsKeyCfgRstPeriod,
                          "NVS cfg: failed to persist rstPeriod");
}

const char* configGetUiLang() {
    return s_uiLangCached;
}

bool configSetUiLang(const char* lang) {
    if (!uiLangSyntaxOk(lang)) {
        return false;
    }
    return setCachedString(s_uiLangCached, sizeof(s_uiLangCached), &s_uiPrefsMux,
                           kNvsKeyCfgUiLang, lang, "NVS cfg: failed to persist ui_lang");
}

const char* configGetUiTheme() {
    return s_uiThemeCached;
}

bool configSetUiTheme(const char* theme) {
    if (!uiThemeSyntaxOk(theme)) {
        return false;
    }
    return setCachedString(s_uiThemeCached, sizeof(s_uiThemeCached), &s_uiPrefsMux,
                           kNvsKeyCfgUiTheme, theme, "NVS cfg: failed to persist ui_theme");
}

void configLoadLedFromNvs() {
    const uint8_t raw = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgLedEn, 1);
    s_ledEnabledCached.store(raw != 0U, std::memory_order_relaxed);
}

bool configGetLedEnabled() {
    return s_ledEnabledCached.load(std::memory_order_relaxed);
}

bool configSetLedEnabled(bool enabled) {
    return setCachedBoolAsUChar(s_ledEnabledCached, enabled, kNvsKeyCfgLedEn,
                                "NVS cfg: failed to persist led_en");
}

void configLoadDisplayViewFromNvs() {
    const auto raw =
        static_cast<DisplayView>(app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgDispView, 0));
    s_displayViewCached.store(displayViewIsValid(raw) ? raw : DisplayView::Unknown,
                              std::memory_order_relaxed);
}

DisplayView configGetDisplayView() {
    return s_displayViewCached.load(std::memory_order_relaxed);
}

bool configSetDisplayView(DisplayView view) {
    if (!displayViewIsValid(view) || view == DisplayView::Unknown) {
        return false;
    }
    return setCachedUChar(s_displayViewCached, view, kNvsKeyCfgDispView,
                          "NVS cfg: failed to persist disp_view");
}

bool configInvalidateDisplayView() {
    // Force a redraw in this boot even if NVS is temporarily unavailable.
    s_displayViewCached.store(DisplayView::Unknown, std::memory_order_relaxed);
    if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgDispView,
                             static_cast<uint8_t>(DisplayView::Unknown))) {
        ESP_LOGE(TAG, "NVS cfg: failed to invalidate disp_view");
        return false;
    }
    return true;
}

void configLoadAudioFromNvs() {
    uint8_t q0 = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndQ0, kAudioDefaultQuiet0);
    uint8_t q1 = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndQ1, kAudioDefaultQuiet1);
    const uint16_t txHz =
        clampAudioToneHz(app_nvs::readUInt(kNvsNsCfg, kNvsKeyCfgSndTxHz, kAudioDefaultTxHz),
                         kAudioDefaultTxHz);
    const uint16_t txMs =
        clampAudioToneMs(app_nvs::readUInt(kNvsNsCfg, kNvsKeyCfgSndTxMs, kAudioDefaultTxMs),
                         kAudioDefaultTxMs);
    const uint16_t rxHz =
        clampAudioToneHz(app_nvs::readUInt(kNvsNsCfg, kNvsKeyCfgSndRxHz, kAudioDefaultRxHz),
                         kAudioDefaultRxHz);
    const uint16_t rxMs =
        clampAudioToneMs(app_nvs::readUInt(kNvsNsCfg, kNvsKeyCfgSndRxMs, kAudioDefaultRxMs),
                         kAudioDefaultRxMs);

    bool txEn = false;
    bool rxEn = false;
    const bool hasTxEn = app_nvs::hasKey(kNvsNsCfg, kNvsKeyCfgSndTxEn);
    const bool hasRxEn = app_nvs::hasKey(kNvsNsCfg, kNvsKeyCfgSndRxEn);
    if (hasTxEn || hasRxEn) {
        txEn = hasTxEn && app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndTxEn, 0) != 0U;
        rxEn = hasRxEn && app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndRxEn, 0) != 0U;
    } else if (app_nvs::hasKey(kNvsNsCfg, kNvsKeyCfgSndMute)) {
        // One-time migration from legacy global mute: unmuted → both kinds on.
        const bool unmuted = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndMute, 0) == 0U;
        txEn               = unmuted;
        rxEn               = unmuted;
        if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndTxEn, txEn ? 1U : 0U)
            || !app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndRxEn, rxEn ? 1U : 0U)) {
            ESP_LOGE(TAG, "NVS cfg: failed to migrate snd_mute → snd_tx_en/snd_rx_en");
        }
    }

    uint8_t txVol = kAudioDefaultVolume;
    uint8_t rxVol = kAudioDefaultVolume;
    const bool hasTxVol = app_nvs::hasKey(kNvsNsCfg, kNvsKeyCfgSndTxVol);
    const bool hasRxVol = app_nvs::hasKey(kNvsNsCfg, kNvsKeyCfgSndRxVol);
    if (hasTxVol || hasRxVol) {
        txVol = hasTxVol ? app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndTxVol, kAudioDefaultVolume)
                         : kAudioDefaultVolume;
        rxVol = hasRxVol ? app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndRxVol, kAudioDefaultVolume)
                         : kAudioDefaultVolume;
    } else if (app_nvs::hasKey(kNvsNsCfg, kNvsKeyCfgSndVol)) {
        const uint8_t legacy = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgSndVol, kAudioDefaultVolume);
        txVol                = legacy;
        rxVol                = legacy;
        if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndTxVol, txVol)
            || !app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgSndRxVol, rxVol)) {
            ESP_LOGE(TAG, "NVS cfg: failed to migrate snd_vol → snd_tx_vol/snd_rx_vol");
        }
    }
    if (txVol > kAudioVolumeMax) {
        txVol = kAudioDefaultVolume;
    }
    if (rxVol > kAudioVolumeMax) {
        rxVol = kAudioDefaultVolume;
    }

    if (q0 > kAudioHourMax) {
        q0 = kAudioDefaultQuiet0;
    }
    if (q1 > kAudioHourMax) {
        q1 = kAudioDefaultQuiet1;
    }
    s_audioTxEnabledCached.store(txEn, std::memory_order_relaxed);
    s_audioRxEnabledCached.store(rxEn, std::memory_order_relaxed);
    s_audioTxVolumeCached.store(txVol, std::memory_order_relaxed);
    s_audioRxVolumeCached.store(rxVol, std::memory_order_relaxed);
    s_audioQuiet0Cached.store(q0, std::memory_order_relaxed);
    s_audioQuiet1Cached.store(q1, std::memory_order_relaxed);
    s_audioTxHzCached.store(txHz, std::memory_order_relaxed);
    s_audioTxMsCached.store(txMs, std::memory_order_relaxed);
    s_audioRxHzCached.store(rxHz, std::memory_order_relaxed);
    s_audioRxMsCached.store(rxMs, std::memory_order_relaxed);
}

bool configGetAudioTxEnabled() {
    return s_audioTxEnabledCached.load(std::memory_order_relaxed);
}

bool configSetAudioTxEnabled(bool enabled) {
    return setCachedBoolAsUChar(s_audioTxEnabledCached, enabled, kNvsKeyCfgSndTxEn,
                                "NVS cfg: failed to persist snd_tx_en");
}

bool configGetAudioRxEnabled() {
    return s_audioRxEnabledCached.load(std::memory_order_relaxed);
}

bool configSetAudioRxEnabled(bool enabled) {
    return setCachedBoolAsUChar(s_audioRxEnabledCached, enabled, kNvsKeyCfgSndRxEn,
                                "NVS cfg: failed to persist snd_rx_en");
}

uint8_t configGetAudioTxVolume() {
    return s_audioTxVolumeCached.load(std::memory_order_relaxed);
}

bool configSetAudioTxVolume(uint8_t volume) {
    if (volume > kAudioVolumeMax) {
        volume = kAudioVolumeMax;
    }
    return setCachedUChar(s_audioTxVolumeCached, volume, kNvsKeyCfgSndTxVol,
                          "NVS cfg: failed to persist snd_tx_vol");
}

uint8_t configGetAudioRxVolume() {
    return s_audioRxVolumeCached.load(std::memory_order_relaxed);
}

bool configSetAudioRxVolume(uint8_t volume) {
    if (volume > kAudioVolumeMax) {
        volume = kAudioVolumeMax;
    }
    return setCachedUChar(s_audioRxVolumeCached, volume, kNvsKeyCfgSndRxVol,
                          "NVS cfg: failed to persist snd_rx_vol");
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

uint16_t configGetAudioTxHz() {
    return s_audioTxHzCached.load(std::memory_order_relaxed);
}

uint16_t configGetAudioTxMs() {
    return s_audioTxMsCached.load(std::memory_order_relaxed);
}

uint16_t configGetAudioRxHz() {
    return s_audioRxHzCached.load(std::memory_order_relaxed);
}

uint16_t configGetAudioRxMs() {
    return s_audioRxMsCached.load(std::memory_order_relaxed);
}

bool configSetAudioTones(uint16_t txHz, uint16_t txMs, uint16_t rxHz, uint16_t rxMs) {
    const AudioTone tx{clampAudioToneHz(txHz, kAudioDefaultTxHz),
                       clampAudioToneMs(txMs, kAudioDefaultTxMs)};
    const AudioTone rx{clampAudioToneHz(rxHz, kAudioDefaultRxHz),
                       clampAudioToneMs(rxMs, kAudioDefaultRxMs)};
    if (configGetAudioTxHz() == tx.hz && configGetAudioTxMs() == tx.ms
        && configGetAudioRxHz() == rx.hz && configGetAudioRxMs() == rx.ms) {
        return true;
    }
    if (!app_nvs::writeUInt(kNvsNsCfg, kNvsKeyCfgSndTxHz, tx.hz)
        || !app_nvs::writeUInt(kNvsNsCfg, kNvsKeyCfgSndTxMs, tx.ms)
        || !app_nvs::writeUInt(kNvsNsCfg, kNvsKeyCfgSndRxHz, rx.hz)
        || !app_nvs::writeUInt(kNvsNsCfg, kNvsKeyCfgSndRxMs, rx.ms)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist snd_tx/rx tone");
        return false;
    }
    s_audioTxHzCached.store(tx.hz, std::memory_order_relaxed);
    s_audioTxMsCached.store(tx.ms, std::memory_order_relaxed);
    s_audioRxHzCached.store(rx.hz, std::memory_order_relaxed);
    s_audioRxMsCached.store(rx.ms, std::memory_order_relaxed);
    return true;
}

void app_configResetRamAfterFactoryClear() {
    s_resetPeriodDaysCached.store(7, std::memory_order_relaxed);
    s_ledEnabledCached.store(true, std::memory_order_relaxed);
    s_displayViewCached.store(DisplayView::Unknown, std::memory_order_relaxed);
    s_audioTxEnabledCached.store(false, std::memory_order_relaxed);
    s_audioRxEnabledCached.store(false, std::memory_order_relaxed);
    s_audioTxVolumeCached.store(kAudioDefaultVolume, std::memory_order_relaxed);
    s_audioRxVolumeCached.store(kAudioDefaultVolume, std::memory_order_relaxed);
    s_audioQuiet0Cached.store(kAudioDefaultQuiet0, std::memory_order_relaxed);
    s_audioQuiet1Cached.store(kAudioDefaultQuiet1, std::memory_order_relaxed);
    s_audioTxHzCached.store(kAudioDefaultTxHz, std::memory_order_relaxed);
    s_audioTxMsCached.store(kAudioDefaultTxMs, std::memory_order_relaxed);
    s_audioRxHzCached.store(kAudioDefaultRxHz, std::memory_order_relaxed);
    s_audioRxMsCached.store(kAudioDefaultRxMs, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_uiPrefsMux);
    strlcpy(s_uiLangCached, "en", sizeof(s_uiLangCached));
    strlcpy(s_uiThemeCached, "light", sizeof(s_uiThemeCached));
    portEXIT_CRITICAL(&s_uiPrefsMux);
}
