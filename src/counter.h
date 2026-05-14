#pragma once

#include <cstdint>
#include <ctime>

/** Incoming heart counter (last value received on topicSub, retained decimal payload); persisted in NVS chaya. */
extern int heartCounter;
/** Outgoing heart counter (successful publishes; next publish sends this + 1); persisted in NVS chaya. */
extern int heartSentCounter;
/** Baselines for periodic display reset (NVS); displayed delta = raw counter minus baseline (capped in display). */
extern int counterBaseline;
extern int sentCountBaseline;

/** UTC calendar day index since epoch (floor(epoch_seconds / 86400)); used for periodic counter reset. */
uint32_t calendarDaySinceEpochUtc(time_t utc);

void loadHeartCounter();
void loadHeartSentCounter();
/** @return true if NVS write succeeded */
bool saveHeartCounter();
/** @return true if NVS write succeeded */
bool saveHeartSentCounter();
/** NVS at most ~30 s after counter change (less flash wear). */
void maybeSaveHeartCounter();
void maybeSaveHeartSentCounter();
/** Immediate save if counter changed since last commit (e.g. before restart). */
void flushHeartCounterIfDirty();
void flushHeartSentCounterIfDirty();

/** Load counter baselines and last reset calendar day from NVS (namespace chaya). */
void loadCounterBaseline();
/** If NTP time is valid, roll display baselines on a UTC-day interval (retained MQTT counters unchanged). */
void maybePeriodicallyResetCounters();
/** If incoming or outgoing display delta reaches 999, reset that baseline so the display shows 0. */
void maybeResetDisplayBaselinesWhenCapped();

/** Load reset interval (days) from NVS into RAM (call once at boot). */
void configLoadResetPeriodFromNvs();

/** Display reset interval: 0 = periodic reset off; 1–30 = UTC calendar days (default when unset in NVS: 7). */
uint8_t configGetResetPeriodDays();
void configSetResetPeriodDays(uint8_t days);

/** Web UI access code (device display); NVS cfg/authEn, default off. */
bool configGetWebAuthEnabled();
void configSetWebAuthEnabled(bool enabled);
void configLoadWebAuthFromNvs();

/**
 * Reset all in-RAM counter/baseline/commit state after NVS clear (factory reset).
 * Do not call flushHeart* after NVS clear — that would recreate chaya namespace.
 */
void counterResetRamAfterFactoryClear();
