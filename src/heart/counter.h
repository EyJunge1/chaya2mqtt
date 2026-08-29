#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>

#include "util/time_helpers.h"

/** Last MQTT sub value (NVS chaya). */
extern std::atomic<int> heartCounter;
/** Successful TX count; next publish sends this + 1 (NVS chaya). */
extern std::atomic<int> heartSentCounter;
/** Display baseline for RX / TX (NVS); delta shown = raw − baseline, capped. */
extern std::atomic<int> counterBaseline;
extern std::atomic<int> sentCountBaseline;

int heartDisplayRxDelta();
int heartDisplayTxDelta();

/** Update remote counter and apply publish-side TX increment under display mux. */
void heartCounterStoreFromRemote(int value);
void heartSentCounterApplyAfterSuccessfulPublish();

struct HeartCounterDrawSnapshot {
    int heartCounterRaw{};
    int counterBaselineRaw{};
    int heartSentCounterRaw{};
    int sentCountBaselineRaw{};
};
void heartCounterFillDrawSnapshot(HeartCounterDrawSnapshot* out);

void loadHeartCounter();
void counterSuspendNvsSavesForFactoryReset();
/** Debounced save for RX + TX (≥30 s). */
void maybeSaveAllHeartCounters();
/** Debounced save for TX only (e.g. after successful publish ack). */
void maybeSaveHeartSentCounter();
/** Immediate flush of RX + TX if dirty (shutdown / factory / OTA). */
void flushAllHeartCountersIfDirty();

void maybePeriodicallyResetCounters();
void maybeResetDisplayBaselinesWhenCapped();

/** RAM reset after factory NVS wipe (no flush — namespace gone). */
void counterResetRamAfterFactoryClear();
