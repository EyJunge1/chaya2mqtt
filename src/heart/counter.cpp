#include "counter.h"
#include "counter_internal.h"
#include "counter_pure.h"

#include "async/sse_dirty.h"
#include "async/system_lifecycle.h"
#include "config/app_config.h"

#include <Arduino.h>
#include <atomic>
#include <freertos/portmacro.h>

portMUX_TYPE s_heartDisplayMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> s_lastResetCalendarDayUtc{UINT32_MAX};

std::atomic<int> heartCounter{0};
std::atomic<int> heartSentCounter{0};
std::atomic<int> counterBaseline{0};
std::atomic<int> sentCountBaseline{0};

static int heartDisplayDelta(std::atomic<int> &counter, std::atomic<int> &baseline) {
    portENTER_CRITICAL(&s_heartDisplayMux);
    const int c = counter.load(std::memory_order_relaxed);
    const int b = baseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    return heartCounterDeltaPure(c, b);
}

int heartDisplayRxDelta() { return heartDisplayDelta(heartCounter, counterBaseline); }

int heartDisplayTxDelta() { return heartDisplayDelta(heartSentCounter, sentCountBaseline); }

void heartCounterStoreFromRemote(int value) {
    portENTER_CRITICAL(&s_heartDisplayMux);
    heartCounter.store(value, std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    sseMarkDirty(kSseChaya);
}

bool heartSentCounterApplyAfterSuccessfulPublish(int expected) {
    bool applied = false;
    int next = 0;
    portENTER_CRITICAL(&s_heartDisplayMux);
    if (heartSentApplyIfAllowed(&next, expected, g_systemShutdownInProgress.load(std::memory_order_acquire),
                               g_chayaNvsWritesSuspended.load(std::memory_order_acquire))) {
        heartSentCounter.store(next, std::memory_order_relaxed);
        applied = true;
    }
    portEXIT_CRITICAL(&s_heartDisplayMux);
    if (applied) {
        sseMarkDirty(kSseChaya);
    }
    return applied;
}

void heartCounterFillDrawSnapshot(HeartCounterDrawSnapshot *out) {
    if (out == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_heartDisplayMux);
    out->heartCounterRaw = heartCounter.load(std::memory_order_relaxed);
    out->counterBaselineRaw = counterBaseline.load(std::memory_order_relaxed);
    out->heartSentCounterRaw = heartSentCounter.load(std::memory_order_relaxed);
    out->sentCountBaselineRaw = sentCountBaseline.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
}

void heartCounterFillChayaDeltas(int *rx, int *tx) {
    HeartCounterDrawSnapshot snap{};
    heartCounterFillDrawSnapshot(&snap);
    if (rx != nullptr) {
        *rx = heartCounterDeltaPure(snap.heartCounterRaw, snap.counterBaselineRaw);
    }
    if (tx != nullptr) {
        *tx = heartCounterDeltaPure(snap.heartSentCounterRaw, snap.sentCountBaselineRaw);
    }
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
    sseMarkDirty(kSseChaya);
}
} // namespace

void counterResetRamAfterFactoryClear() {
    counterResetValuesRam();
    app_configResetRamAfterFactoryClear();
}
