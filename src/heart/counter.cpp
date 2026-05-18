#include "counter.h"

#include "async/task_handles.h"
#include "config/app_config.h"
#include "constants.h"
#include "display/display.h"
#include "config/nvs_utils.h"
#include "wifi/wlan.h"

#include "log_tag.h"

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/portmacro.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <time.h>

DEFINE_LOG_TAG("CTR");

namespace {
constexpr const char kNvsNsChaya[] = "chaya";
static std::atomic<bool> s_chayaNvsWritesSuspended{false};
static portMUX_TYPE      s_heartDisplayMux = portMUX_INITIALIZER_UNLOCKED;
} // namespace

std::atomic<int> heartCounter{0};
std::atomic<int> heartSentCounter{0};

std::atomic<int> counterBaseline{0};
std::atomic<int> sentCountBaseline{0};

static std::atomic<uint32_t> s_lastResetCalendarDayUtc{UINT32_MAX};

namespace {

constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000UL;

static bool chayaNvsWritesAllowed() {
    return !s_chayaNvsWritesSuspended.load(std::memory_order_acquire);
}

// NVS int with ≥30s coalesce to limit flash wear.
class DebouncedChayaCounter {
public:
    DebouncedChayaCounter(std::atomic<int>* value, const char* nvsKey, const char* saveFailMsg)
        : value_(value)
        , nvsKey_(nvsKey)
        , saveFailMsg_(saveFailMsg)
        , lastCommitted_(value->load(std::memory_order_relaxed))
        , lastSaveMs_(0) {}

    void syncAfterExternalLoad(unsigned long ms) {
        lastCommitted_ = value_->load(std::memory_order_relaxed);
        lastSaveMs_    = ms;
    }

    void resetCommittedAndTimestamps(unsigned long ms) {
        lastCommitted_ = value_->load(std::memory_order_relaxed);
        lastSaveMs_    = ms;
    }

    bool save() {
        if (!chayaNvsWritesAllowed()) {
            return false;
        }
        const int v = value_->load(std::memory_order_relaxed);
        if (!app_nvs::writeInt(kNvsNsChaya, nvsKey_, v)) {
            ESP_LOGE(TAG, "%s", saveFailMsg_);
            return false;
        }
        return true;
    }

    void maybeSave() {
        const int v = value_->load(std::memory_order_relaxed);
        if (v == lastCommitted_) {
            return;
        }
        const unsigned long now = millis();
        if (now - lastSaveMs_ >= kHeartCounterSaveMinIntervalMs) {
            if (save()) {
                lastCommitted_ = v;
                lastSaveMs_    = now;
            }
        }
    }

    void flushIfDirty() {
        const int v = value_->load(std::memory_order_relaxed);
        if (v != lastCommitted_) {
            if (save()) {
                lastCommitted_ = v;
                lastSaveMs_    = millis();
            }
        }
    }

private:
    std::atomic<int>*   value_;
    const char*         nvsKey_;
    const char*         saveFailMsg_;
    int                 lastCommitted_;
    unsigned long       lastSaveMs_;
};

DebouncedChayaCounter s_rxCounter(&heartCounter, "counter", "NVS chaya: write counter failed");
DebouncedChayaCounter s_txCounter(&heartSentCounter, "sentCount", "NVS chaya: write sentCount failed");

inline void heartDebounceLock() {
    if (g_heartDebounceMutex != nullptr) {
        xSemaphoreTake(g_heartDebounceMutex, portMAX_DELAY);
    }
}

inline void heartDebounceUnlock() {
    if (g_heartDebounceMutex != nullptr) {
        xSemaphoreGive(g_heartDebounceMutex);
    }
}

} // namespace

uint32_t calendarDaySinceEpochUtc(time_t utc) {
    if (utc < 0) {
        utc = 0;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(utc) / 86400ULL);
}

void counterSuspendNvsSavesForFactoryReset() {
    s_chayaNvsWritesSuspended.store(true, std::memory_order_release);
}

int heartDisplayRxDelta() {
    portENTER_CRITICAL(&s_heartDisplayMux);
    const int c = heartCounter.load(std::memory_order_relaxed);
    const int b = counterBaseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    return (c > b) ? (c - b) : 0;
}

int heartDisplayTxDelta() {
    portENTER_CRITICAL(&s_heartDisplayMux);
    const int c = heartSentCounter.load(std::memory_order_relaxed);
    const int b = sentCountBaseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    return (c > b) ? (c - b) : 0;
}

void heartCounterFillDrawSnapshot(HeartCounterDrawSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_heartDisplayMux);
    out->heartCounterRaw         = heartCounter.load(std::memory_order_relaxed);
    out->counterBaselineRaw      = counterBaseline.load(std::memory_order_relaxed);
    out->heartSentCounterRaw     = heartSentCounter.load(std::memory_order_relaxed);
    out->sentCountBaselineRaw = sentCountBaseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
}

static bool persistCounterBaselineState() {
    if (!chayaNvsWritesAllowed()) {
        return false;
    }
    int         snapCntBase      = 0;
    int         snapSntBase      = 0;
    uint32_t    snapRstDay       = UINT32_MAX;
    portENTER_CRITICAL(&s_heartDisplayMux);
    snapCntBase = counterBaseline.load(std::memory_order_relaxed);
    snapSntBase = sentCountBaseline.load(std::memory_order_relaxed);
    snapRstDay  = s_lastResetCalendarDayUtc.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);

    app_nvs::ScopedNvsLock lock;
    Preferences            prefs;
    if (!prefs.begin(kNvsNsChaya, false)) {
        ESP_LOGE(TAG, "NVS chaya: open for baseline write failed");
        return false;
    }
    const bool okCnt = prefs.putInt("cntBase", snapCntBase) > 0U;
    const bool okSnt = prefs.putInt("sntBase", snapSntBase) > 0U;
    const bool okDay = prefs.putUInt("rstDay", snapRstDay) > 0U;
    prefs.end();
    if (!okCnt || !okSnt || !okDay) {
        ESP_LOGE(TAG, "NVS chaya: baseline write failed");
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

    if (s_lastResetCalendarDayUtc.load(std::memory_order_relaxed) == UINT32_MAX) {
        s_lastResetCalendarDayUtc.store(currentDay, std::memory_order_relaxed);
        if (!persistCounterBaselineState()) {
            s_lastResetCalendarDayUtc.store(UINT32_MAX, std::memory_order_relaxed);
        }
        return;
    }

    const uint8_t periodDays = configGetResetPeriodDays();
    if (periodDays == 0U) {
        return;
    }

    const uint32_t lastDay = s_lastResetCalendarDayUtc.load(std::memory_order_relaxed);
    const uint32_t daysSinceReset
        = (currentDay >= lastDay) ? (currentDay - lastDay) : 0U;
    const bool      shouldReset = (daysSinceReset >= static_cast<uint32_t>(periodDays));

    if (!shouldReset) {
        return;
    }

    portENTER_CRITICAL(&s_heartDisplayMux);
    counterBaseline.store(heartCounter.load(std::memory_order_relaxed), std::memory_order_relaxed);
    sentCountBaseline.store(heartSentCounter.load(std::memory_order_relaxed), std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    s_lastResetCalendarDayUtc.store(currentDay, std::memory_order_relaxed);
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
    const int64_t dRecv = static_cast<int64_t>(heartCounter.load(std::memory_order_relaxed))
                          - static_cast<int64_t>(counterBaseline.load(std::memory_order_relaxed));
    const int64_t dSent = static_cast<int64_t>(heartSentCounter.load(std::memory_order_relaxed))
                          - static_cast<int64_t>(sentCountBaseline.load(std::memory_order_relaxed));
    if (dRecv >= kDisplayCounterMax) {
        portENTER_CRITICAL(&s_heartDisplayMux);
        counterBaseline.store(heartCounter.load(std::memory_order_relaxed), std::memory_order_relaxed);
        portEXIT_CRITICAL(&s_heartDisplayMux);
        changed = true;
    }
    if (dSent >= kDisplayCounterMax) {
        portENTER_CRITICAL(&s_heartDisplayMux);
        sentCountBaseline.store(heartSentCounter.load(std::memory_order_relaxed), std::memory_order_relaxed);
        portEXIT_CRITICAL(&s_heartDisplayMux);
        changed = true;
    }
    if (changed && persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Display baseline reset (display reached cap)");
        requestHeartRedraw();
    }
}

void loadHeartCounter() {
    const unsigned long t = millis();
    app_nvs::ScopedNvsLock lock;
    Preferences            prefs;
    if (!prefs.begin(kNvsNsChaya, true)) {
        ESP_LOGI(TAG, "NVS chaya namespace not present yet, counters = 0");
        heartCounter.store(0, std::memory_order_relaxed);
        heartSentCounter.store(0, std::memory_order_relaxed);
        counterBaseline.store(0, std::memory_order_relaxed);
        sentCountBaseline.store(0, std::memory_order_relaxed);
        s_lastResetCalendarDayUtc.store(UINT32_MAX, std::memory_order_relaxed);
        heartDebounceLock();
        s_rxCounter.resetCommittedAndTimestamps(t);
        s_txCounter.resetCommittedAndTimestamps(t);
        heartDebounceUnlock();
        return;
    }
    heartCounter.store(std::max<int32_t>(prefs.getInt("counter", 0), 0), std::memory_order_relaxed);
    heartSentCounter.store(std::max<int32_t>(prefs.getInt("sentCount", 0), 0),
                           std::memory_order_relaxed);
    counterBaseline.store(std::max<int32_t>(prefs.getInt("cntBase", 0), 0), std::memory_order_relaxed);
    sentCountBaseline.store(std::max<int32_t>(prefs.getInt("sntBase", 0), 0), std::memory_order_relaxed);
    s_lastResetCalendarDayUtc.store(prefs.getUInt("rstDay", UINT32_MAX), std::memory_order_relaxed);
    prefs.end();

    ESP_LOGD(TAG,
             "Counters loaded from NVS: counter=%d sent=%d cntBase=%d sntBase=%d rstDay=%" PRIu32,
             heartCounter.load(std::memory_order_relaxed),
             heartSentCounter.load(std::memory_order_relaxed),
             counterBaseline.load(std::memory_order_relaxed),
             sentCountBaseline.load(std::memory_order_relaxed),
             s_lastResetCalendarDayUtc.load(std::memory_order_relaxed));

    heartDebounceLock();
    s_rxCounter.syncAfterExternalLoad(t);
    s_txCounter.syncAfterExternalLoad(t);
    heartDebounceUnlock();
}

bool saveHeartCounter() {
    heartDebounceLock();
    const bool ok = s_rxCounter.save();
    if (ok) {
        s_rxCounter.syncAfterExternalLoad(millis());
    }
    heartDebounceUnlock();
    return ok;
}

bool saveHeartSentCounter() {
    heartDebounceLock();
    const bool ok = s_txCounter.save();
    if (ok) {
        s_txCounter.syncAfterExternalLoad(millis());
    }
    heartDebounceUnlock();
    return ok;
}

void maybeSaveHeartCounter() {
    heartDebounceLock();
    s_rxCounter.maybeSave();
    heartDebounceUnlock();
}

void maybeSaveHeartSentCounter() {
    heartDebounceLock();
    s_txCounter.maybeSave();
    heartDebounceUnlock();
}

void flushHeartCounterIfDirty() {
    heartDebounceLock();
    s_rxCounter.flushIfDirty();
    heartDebounceUnlock();
}

void flushHeartSentCounterIfDirty() {
    heartDebounceLock();
    s_txCounter.flushIfDirty();
    heartDebounceUnlock();
}

void counterResetRamAfterFactoryClear() {
    heartCounter.store(0, std::memory_order_relaxed);
    heartSentCounter.store(0, std::memory_order_relaxed);
    counterBaseline.store(0, std::memory_order_relaxed);
    sentCountBaseline.store(0, std::memory_order_relaxed);
    s_lastResetCalendarDayUtc.store(UINT32_MAX, std::memory_order_relaxed);
    const unsigned long t = millis();
    heartDebounceLock();
    s_rxCounter.resetCommittedAndTimestamps(t);
    s_txCounter.resetCommittedAndTimestamps(t);
    heartDebounceUnlock();
    app_configResetRamAfterFactoryClear();
}
