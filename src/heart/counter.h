#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>

/** Last MQTT sub value (NVS chaya). */
extern std::atomic<int> heartCounter;
/** Successful TX count; next publish sends this + 1 (NVS chaya). */
extern std::atomic<int> heartSentCounter;
/** Display baseline for RX / TX (NVS); delta shown = raw − baseline, capped. */
extern std::atomic<int> counterBaseline;
extern std::atomic<int> sentCountBaseline;

int heartDisplayRxDelta();
int heartDisplayTxDelta();

struct HeartCounterDrawSnapshot {
    int heartCounterRaw{};
    int counterBaselineRaw{};
    int heartSentCounterRaw{};
    int sentCountBaselineRaw{};
};
void heartCounterFillDrawSnapshot(HeartCounterDrawSnapshot* out);

/** UTC day index: floor(utc / 86400). */
uint32_t calendarDaySinceEpochUtc(time_t utc);

void loadHeartCounter();
void counterSuspendNvsSavesForFactoryReset();
bool saveHeartCounter();
bool saveHeartSentCounter();
void maybeSaveHeartCounter();
void maybeSaveHeartSentCounter();
void flushHeartCounterIfDirty();
void flushHeartSentCounterIfDirty();

void maybePeriodicallyResetCounters();
void maybeResetDisplayBaselinesWhenCapped();

/** RAM reset after factory NVS wipe (no flush — namespace gone). */
void counterResetRamAfterFactoryClear();
