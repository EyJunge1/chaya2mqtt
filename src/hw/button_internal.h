#pragma once

#include <atomic>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
};

extern std::atomic<TaskHandle_t> s_buttonTaskHandle;
extern std::atomic<LedTxPhase>   ledTxPhase;
extern unsigned long             ledPhaseStartMs;
extern unsigned long             ledPhaseDurationMs;

struct ButtonState {
    bool          heldDown             = false;
    unsigned long pressStartMs         = 0;
    int           lastRawReading        = 0;
    unsigned long lastDebounceChangeMs  = 0;
    int           debouncedLevel        = 0;
};

struct PwrButtonState {
    bool          seenRelease           = false;
    bool          heldDown              = false;
    bool          softOffTriggered      = false;
    unsigned long pressStartMs          = 0;
    int           lastRawReading        = 0;
    unsigned long lastDebounceChangeMs  = 0;
    int           debouncedLevel        = 0;
};

extern ButtonState    btn;
extern PwrButtonState pwr;

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

void buttonPollAndProcess();
void pwrPollAndProcess();
