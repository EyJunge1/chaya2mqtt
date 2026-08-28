#pragma once

#include <atomic>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern std::atomic<TaskHandle_t> s_buttonTaskHandle;

struct ButtonState {
    bool          heldDown            = false;
    unsigned long pressStartMs        = 0;
    int           lastRawReading       = 0;
    unsigned long lastDebounceChangeMs = 0;
    int           debouncedLevel       = 0;
};

struct PwrButtonState {
    bool          seenRelease          = false;
    bool          heldDown             = false;
    /** True once this press has been held ≥ kSoftOffHoldMs (LED ack). Soft-off runs on release. */
    bool          softOffArmed         = false;
    unsigned long pressStartMs         = 0;
    int           lastRawReading       = 0;
    unsigned long lastDebounceChangeMs = 0;
    int           debouncedLevel       = 0;
};

extern ButtonState    btn;
extern PwrButtonState pwr;

void buttonPollAndProcess();
void pwrPollAndProcess();
