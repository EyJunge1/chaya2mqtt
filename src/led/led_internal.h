#pragma once

#include <atomic>
#include <cstdint>

enum class LedTxPhase : uint8_t {
    Idle,
    PreOn1,
    PreOff1,
    PreOn2,
    PreOff2,
    PublishTry,
    PostWait,
    PostOn1,
    PostOff1,
    PostOn2,
    PostOff2,
    FailOn1,
    FailOff1,
    FailOn2,
    FailOff2,
    FailOn3,
    FailOff3,
    RefreshOn,
    RefreshOff,
    PatternOn,
    PatternOff,
};

extern std::atomic<LedTxPhase> ledTxPhase;
extern unsigned long           ledPhaseStartMs;
extern unsigned long           ledPhaseDurationMs;

void armLedPhase(unsigned long durationMs);
void ledOutput(int level);
/** Internal active-low LED write that bypasses the user preference. */
void ledOutputForced(int level);
void ledHoldWhenIdle();

void advanceLedSequence();
void startMqttSendLedSequence();
bool ledSendSequenceActive();
bool ledTxBusy();
bool ledActivityActive();
