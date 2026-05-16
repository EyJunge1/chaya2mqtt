#include "counter.h"

#include "config/app_config.h"
#include "constants.h"
#include "display/display.h"
#include "config/nvs_utils.h"
#include "wifi/wlan.h"

#include "log_tag.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <time.h>

DEFINE_LOG_TAG("CTR");

namespace {
constexpr const char kNvsNsChaya[] = "chaya";
} // namespace

int heartCounter     = 0;
int heartSentCounter = 0;

int counterBaseline   = 0;
int sentCountBaseline = 0;

static uint32_t s_lastResetCalendarDayUtc = UINT32_MAX;

namespace {

constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000UL;

/** NVS-backed int with periodic commit (same namespace "chaya") to reduce flash wear. */
class DebouncedChayaCounter {
public:
    DebouncedChayaCounter(int* value, const char* nvsKey, const char* saveFailMsg)
        : value_(value)
        , nvsKey_(nvsKey)
        , saveFailMsg_(saveFailMsg)
        , lastCommitted_(*value)
        , lastSaveMs_(0) {}

    void syncAfterExternalLoad(unsigned long ms) {
        lastCommitted_ = *value_;
        lastSaveMs_    = ms;
    }

    void resetCommittedAndTimestamps(unsigned long ms) {
        lastCommitted_ = *value_;
        lastSaveMs_    = ms;
    }

    bool save() {
        if (!app_nvs::writeInt(kNvsNsChaya, nvsKey_, *value_)) {
            ESP_LOGE(TAG, "%s", saveFailMsg_);
            return false;
        }
        return true;
    }

    void maybeSave() {
        if (*value_ == lastCommitted_) {
            return;
        }
        const unsigned long now = millis();
        if (now - lastSaveMs_ >= kHeartCounterSaveMinIntervalMs) {
            if (save()) {
                lastCommitted_ = *value_;
                lastSaveMs_    = now;
            }
        }
    }

    void flushIfDirty() {
        if (*value_ != lastCommitted_) {
            if (save()) {
                lastCommitted_ = *value_;
                lastSaveMs_    = millis();
            }
        }
    }

private:
    int*                value_;
    const char*         nvsKey_;
    const char*         saveFailMsg_;
    int                 lastCommitted_;
    unsigned long       lastSaveMs_;
};

DebouncedChayaCounter s_rxCounter(&heartCounter, "counter", "NVS chaya: write counter failed");
DebouncedChayaCounter s_txCounter(&heartSentCounter, "sentCount", "NVS chaya: write sentCount failed");

} // namespace

uint32_t calendarDaySinceEpochUtc(time_t utc) {
    if (utc < 0) {
        utc = 0;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(utc) / 86400ULL);
}

void loadCounterBaseline() {
    if (!app_nvs::namespaceExists(kNvsNsChaya)) {
        ESP_LOGI(TAG, "NVS chaya namespace not present yet, baselines = 0");
        counterBaseline           = 0;
        sentCountBaseline         = 0;
        s_lastResetCalendarDayUtc = UINT32_MAX;
        return;
    }
    counterBaseline = std::max<int32_t>(app_nvs::readInt(kNvsNsChaya, "cntBase", 0), 0);
    sentCountBaseline
        = std::max<int32_t>(app_nvs::readInt(kNvsNsChaya, "sntBase", 0), 0);
    s_lastResetCalendarDayUtc =
        app_nvs::readUInt(kNvsNsChaya, "rstDay", UINT32_MAX);
}

static bool persistCounterBaselineState() {
    if (!app_nvs::writeInt(kNvsNsChaya, "cntBase", counterBaseline)) {
        ESP_LOGE(TAG, "NVS chaya: failed to write baseline");
        return false;
    }
    if (!app_nvs::writeInt(kNvsNsChaya, "sntBase", sentCountBaseline)) {
        ESP_LOGE(TAG, "NVS chaya: failed to write baseline");
        return false;
    }
    if (!app_nvs::writeUInt(kNvsNsChaya, "rstDay", s_lastResetCalendarDayUtc)) {
        ESP_LOGE(TAG, "NVS chaya: failed to write baseline");
        return false;
    }
    return true;
}

void maybePeriodicallyResetCounters() {
    if (configIsApMode()) {
        return;
    }
    const time_t utcNow = time(nullptr);
    if (!ntpTimeLooksSynced(utcNow)) {
        return;
    }
    const uint32_t currentDay = calendarDaySinceEpochUtc(utcNow);

    if (s_lastResetCalendarDayUtc == UINT32_MAX) {
        s_lastResetCalendarDayUtc = currentDay;
        if (!persistCounterBaselineState()) {
            s_lastResetCalendarDayUtc = UINT32_MAX;
        }
        return;
    }

    const uint8_t periodDays = configGetResetPeriodDays();
    if (periodDays == 0U) {
        return;
    }

    const uint32_t daysSinceReset
        = (currentDay >= s_lastResetCalendarDayUtc) ? (currentDay - s_lastResetCalendarDayUtc) : 0U;
    const bool      shouldReset = (daysSinceReset >= static_cast<uint32_t>(periodDays));

    if (!shouldReset) {
        return;
    }

    counterBaseline           = heartCounter;
    sentCountBaseline         = heartSentCounter;
    s_lastResetCalendarDayUtc = currentDay;
    if (persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Periodic display counter reset (%u days)", static_cast<unsigned>(periodDays));
        requestHeartRedraw();
    }
}

void maybeResetDisplayBaselinesWhenCapped() {
    if (configIsApMode()) {
        return;
    }
    bool changed = false;
    const int64_t dRecv = static_cast<int64_t>(heartCounter) - static_cast<int64_t>(counterBaseline);
    const int64_t dSent = static_cast<int64_t>(heartSentCounter) - static_cast<int64_t>(sentCountBaseline);
    if (dRecv >= kDisplayCounterMax) {
        counterBaseline = heartCounter;
        changed         = true;
    }
    if (dSent >= kDisplayCounterMax) {
        sentCountBaseline = heartSentCounter;
        changed           = true;
    }
    if (changed && persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Display baseline reset (display reached cap)");
        requestHeartRedraw();
    }
}

void loadHeartCounter() {
    const unsigned long t = millis();
    if (!app_nvs::namespaceExists(kNvsNsChaya)) {
        ESP_LOGI(TAG, "NVS chaya namespace not present yet, counters = 0");
        heartCounter     = 0;
        heartSentCounter = 0;
        s_rxCounter.resetCommittedAndTimestamps(t);
        s_txCounter.resetCommittedAndTimestamps(t);
        loadCounterBaseline();
        return;
    }
    heartCounter     = std::max<int32_t>(app_nvs::readInt(kNvsNsChaya, "counter", 0), 0);
    heartSentCounter = std::max<int32_t>(app_nvs::readInt(kNvsNsChaya, "sentCount", 0), 0);
    s_rxCounter.syncAfterExternalLoad(t);
    s_txCounter.syncAfterExternalLoad(t);
    loadCounterBaseline();
}

bool saveHeartCounter() {
    const bool ok = s_rxCounter.save();
    if (ok) {
        s_rxCounter.syncAfterExternalLoad(millis());
    }
    return ok;
}

bool saveHeartSentCounter() {
    const bool ok = s_txCounter.save();
    if (ok) {
        s_txCounter.syncAfterExternalLoad(millis());
    }
    return ok;
}

void maybeSaveHeartCounter() {
    s_rxCounter.maybeSave();
}

void maybeSaveHeartSentCounter() {
    s_txCounter.maybeSave();
}

void flushHeartCounterIfDirty() {
    s_rxCounter.flushIfDirty();
}

void flushHeartSentCounterIfDirty() {
    s_txCounter.flushIfDirty();
}

void counterResetRamAfterFactoryClear() {
    heartCounter              = 0;
    heartSentCounter          = 0;
    counterBaseline           = 0;
    sentCountBaseline         = 0;
    s_lastResetCalendarDayUtc = UINT32_MAX;
    const unsigned long t = millis();
    s_rxCounter.resetCommittedAndTimestamps(t);
    s_txCounter.resetCommittedAndTimestamps(t);
    app_configResetRamAfterFactoryClear();
}
