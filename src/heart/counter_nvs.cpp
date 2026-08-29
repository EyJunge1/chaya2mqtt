#include "counter.h"
#include "counter_internal.h"

#include "config/nvs_utils.h"
#include "constants.h"
#include "util/time_helpers.h"

#include "util/log_tag.h"

#include <algorithm>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("CTR");

DebouncedChayaCounter::DebouncedChayaCounter(std::atomic<int>* value, const char* nvsKey,
                                             const char* saveFailMsg)
    : value_(value)
    , nvsKey_(nvsKey)
    , saveFailMsg_(saveFailMsg)
    , lastCommitted_(value->load(std::memory_order_relaxed))
    , lastSaveMs_(0) {}

void DebouncedChayaCounter::syncAfterExternalLoad(unsigned long ms) {
    lastCommitted_ = value_->load(std::memory_order_relaxed);
    lastSaveMs_    = ms;
}

void DebouncedChayaCounter::resetCommittedAndTimestamps(unsigned long ms) {
    lastCommitted_ = value_->load(std::memory_order_relaxed);
    lastSaveMs_    = ms;
}

bool DebouncedChayaCounter::save() {
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

void DebouncedChayaCounter::maybeSave() {
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

void DebouncedChayaCounter::flushIfDirty() {
    const int v = value_->load(std::memory_order_relaxed);
    if (v != lastCommitted_) {
        if (save()) {
            lastCommitted_ = v;
            lastSaveMs_    = millis();
        }
    }
}

DebouncedChayaCounter s_rxCounter(&heartCounter, kNvsKeyChayaCounter, "NVS chaya: write counter failed");
DebouncedChayaCounter s_txCounter(&heartSentCounter, kNvsKeyChayaSentCount,
                                  "NVS chaya: write sentCount failed");

bool chayaNvsWritesAllowed() {
    return !s_chayaNvsWritesSuspended.load(std::memory_order_acquire);
}

void loadBaselineFromNvs(Preferences& prefs, int32_t* cntBase, int32_t* sntBase, uint32_t* rstDay) {
    ChayaBaselineBlob blob{};
    const size_t blobLen = prefs.getBytesLength(kNvsKeyChayaBaselineBlob);
    if (blobLen == sizeof(blob)
        && prefs.getBytes(kNvsKeyChayaBaselineBlob, &blob, sizeof(blob)) == sizeof(blob)) {
        *cntBase = blob.cntBase;
        *sntBase = blob.sntBase;
        *rstDay  = blob.rstDay;
        return;
    }
    *cntBase = prefs.getInt(kNvsKeyChayaCntBase, 0);
    *sntBase = prefs.getInt(kNvsKeyChayaSntBase, 0);
    *rstDay  = prefs.getUInt(kNvsKeyChayaRstDay, UINT32_MAX);
}

void counterSuspendNvsSavesForFactoryReset() {
    s_chayaNvsWritesSuspended.store(true, std::memory_order_release);
}

void loadHeartCounter() {
    const unsigned long t = millis();
    int32_t             loadedCounter = 0;
    int32_t             loadedSent    = 0;
    int32_t             loadedCntBase = 0;
    int32_t             loadedSntBase = 0;
    uint32_t            loadedRstDay  = UINT32_MAX;
    bool                nvsPresent    = false;

    {
        app_nvs::ScopedNvsLock lock;
        Preferences            prefs;
        if (prefs.begin(kNvsNsChaya, true)) {
            nvsPresent = true;
            loadedCounter = std::max<int32_t>(prefs.getInt(kNvsKeyChayaCounter, 0), 0);
            loadedSent    = std::max<int32_t>(prefs.getInt(kNvsKeyChayaSentCount, 0), 0);
            loadBaselineFromNvs(prefs, &loadedCntBase, &loadedSntBase, &loadedRstDay);
            prefs.end();
        }
    }

    if (!nvsPresent) {
        ESP_LOGI(TAG, "NVS chaya namespace not present yet, counters = 0");
    } else {
        const time_t utcNow = time(nullptr);
        if (ntpTimeLooksSynced(utcNow)) {
            const uint32_t today = calendarDaySinceEpochUtc(utcNow);
            if (loadedRstDay != UINT32_MAX && loadedRstDay > today + 1U) {
                ESP_LOGW(TAG, "rstDay in future — clamping to today (%" PRIu32 ")", today);
                loadedRstDay = today;
            }
        }
        ESP_LOGD(TAG,
                 "Counters loaded from NVS: counter=%d sent=%d cntBase=%d sntBase=%d rstDay=%" PRIu32,
                 loadedCounter, loadedSent, loadedCntBase, loadedSntBase, loadedRstDay);
    }

    heartCounter.store(loadedCounter, std::memory_order_relaxed);
    heartSentCounter.store(loadedSent, std::memory_order_relaxed);
    counterBaseline.store(std::max<int32_t>(loadedCntBase, 0), std::memory_order_relaxed);
    sentCountBaseline.store(std::max<int32_t>(loadedSntBase, 0), std::memory_order_relaxed);
    s_lastResetCalendarDayUtc.store(loadedRstDay, std::memory_order_relaxed);

    heartDebounceLock();
    s_rxCounter.syncAfterExternalLoad(t);
    s_txCounter.syncAfterExternalLoad(t);
    heartDebounceUnlock();
}

void maybeSaveAllHeartCounters() {
    if (!heartDebounceLockTimed(1000U)) {
        return;
    }
    s_rxCounter.maybeSave();
    s_txCounter.maybeSave();
    heartDebounceUnlock();
}

void maybeSaveHeartSentCounter() {
    if (!heartDebounceLockTimed(1000U)) {
        return;
    }
    s_txCounter.maybeSave();
    heartDebounceUnlock();
}

void flushAllHeartCountersIfDirty() {
    heartDebounceLock();
    s_rxCounter.flushIfDirty();
    s_txCounter.flushIfDirty();
    heartDebounceUnlock();
}
