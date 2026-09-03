#pragma once

#include "async/task_handles.h"
#include "config/nvs_keys.h"

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <cstdint>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>

extern std::atomic<int> heartCounter;
extern std::atomic<int> heartSentCounter;
extern std::atomic<int> counterBaseline;
extern std::atomic<int> sentCountBaseline;

extern std::atomic<bool> s_chayaNvsWritesSuspended;
extern portMUX_TYPE s_heartDisplayMux;
extern std::atomic<uint32_t> s_lastResetCalendarDayUtc;

constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000UL;

struct ChayaBaselineBlob {
    int32_t cntBase;
    int32_t sntBase;
    uint32_t rstDay;
};

static_assert(sizeof(ChayaBaselineBlob) == 12, "ChayaBaselineBlob layout");

class DebouncedChayaCounter {
  public:
    DebouncedChayaCounter(std::atomic<int> *value, const char *nvsKey, const char *saveFailMsg);

    void syncAfterExternalLoad(unsigned long ms);
    void resetCommittedAndTimestamps(unsigned long ms);
    bool save();
    void maybeSave();
    void flushIfDirty();

  private:
    std::atomic<int> *value_;
    const char *nvsKey_;
    const char *saveFailMsg_;
    int lastCommitted_;
    unsigned long lastSaveMs_;
};

extern DebouncedChayaCounter s_rxCounter;
extern DebouncedChayaCounter s_txCounter;

bool chayaNvsWritesAllowed();

void loadBaselineFromNvs(Preferences &prefs, int32_t *cntBase, int32_t *sntBase, uint32_t *rstDay);
bool persistCounterBaselineState();

inline void heartDebounceLock() {
    if (g_heartDebounceMutex != nullptr) {
        xSemaphoreTake(g_heartDebounceMutex, portMAX_DELAY);
    }
}

inline bool heartDebounceLockTimed(uint32_t timeoutMs) {
    if (g_heartDebounceMutex == nullptr) {
        return true;
    }
    return xSemaphoreTake(g_heartDebounceMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

inline void heartDebounceUnlock() {
    if (g_heartDebounceMutex != nullptr) {
        xSemaphoreGive(g_heartDebounceMutex);
    }
}
