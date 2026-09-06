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
extern unsigned long ledPhaseStartMs;
extern unsigned long ledPhaseDurationMs;

void armLedPhase(unsigned long durationMs);
void ledOutput(int level);
/** Internal active-low LED write that bypasses the user preference. */
void ledOutputForced(int level);

void advanceLedSequence();
auto startMqttSendLedSequence() -> bool;
auto ledSendSequenceActive() -> bool;
auto ledTxBusy() -> bool;
auto ledActivityActive() -> bool;

/** TX send may CAS-steal these phases (priority: TX > pattern > refresh). */
inline auto ledTxPhaseAllowsSendStart(LedTxPhase phase) -> bool {
    return phase == LedTxPhase::Idle || phase == LedTxPhase::RefreshOn || phase == LedTxPhase::RefreshOff ||
           phase == LedTxPhase::PatternOn || phase == LedTxPhase::PatternOff;
}

/** finishToIdleOrBackground may CAS only from these phases (RC-UI-01). */
inline auto ledTxPhaseCanFinishToBackground(LedTxPhase phase) -> bool {
    return ledTxPhaseAllowsSendStart(phase) || phase == LedTxPhase::PostOff2 || phase == LedTxPhase::FailOff3;
}
