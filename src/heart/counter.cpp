#include "counter.h"
#include "counter_internal.h"
#include "counter_pure.h"

#include "config/app_config.h"

#include <Arduino.h>
#include <atomic>
#include <freertos/portmacro.h>

std::atomic<bool>     s_chayaNvsWritesSuspended{false};
portMUX_TYPE          s_heartDisplayMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> s_lastResetCalendarDayUtc{UINT32_MAX};

std::atomic<int> heartCounter{0};
std::atomic<int> heartSentCounter{0};
std::atomic<int> counterBaseline{0};
std::atomic<int> sentCountBaseline{0};

int heartDisplayRxDelta() {
    portENTER_CRITICAL(&s_heartDisplayMux);
    const int c = heartCounter.load(std::memory_order_relaxed);
    const int b = counterBaseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    return heartCounterDeltaPure(c, b);
}

int heartDisplayTxDelta() {
    portENTER_CRITICAL(&s_heartDisplayMux);
    const int c = heartSentCounter.load(std::memory_order_relaxed);
    const int b = sentCountBaseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    return heartCounterDeltaPure(c, b);
}

void heartCounterStoreFromRemote(int value) {
    portENTER_CRITICAL(&s_heartDisplayMux);
    heartCounter.store(value, std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
}

void heartSentCounterApplyAfterSuccessfulPublish() {
    portENTER_CRITICAL(&s_heartDisplayMux);
    const int cur = heartSentCounter.load(std::memory_order_relaxed);
    heartSentCounter.store(heartSentCounterNextPure(cur), std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
}

void heartCounterFillDrawSnapshot(HeartCounterDrawSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_heartDisplayMux);
    out->heartCounterRaw      = heartCounter.load(std::memory_order_relaxed);
    out->counterBaselineRaw   = counterBaseline.load(std::memory_order_relaxed);
    out->heartSentCounterRaw  = heartSentCounter.load(std::memory_order_relaxed);
    out->sentCountBaselineRaw = sentCountBaseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
}

namespace {
void counterResetAtomics() {
    portENTER_CRITICAL(&s_heartDisplayMux);
    heartCounter.store(0, std::memory_order_relaxed);
    heartSentCounter.store(0, std::memory_order_relaxed);
    counterBaseline.store(0, std::memory_order_relaxed);
    sentCountBaseline.store(0, std::memory_order_relaxed);
    s_lastResetCalendarDayUtc.store(UINT32_MAX, std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
}

void counterResetValuesRam() {
    counterResetAtomics();
    const unsigned long t = millis();
    heartDebounceLock();
    s_rxCounter.resetCommittedAndTimestamps(t);
    s_txCounter.resetCommittedAndTimestamps(t);
    heartDebounceUnlock();
}
} // namespace

void counterResetRamAfterFactoryClear() {
    counterResetValuesRam();
    app_configResetRamAfterFactoryClear();
}
