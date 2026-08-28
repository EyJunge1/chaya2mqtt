#pragma once

#include "pins.h"

#include <cstdint>

/** Button GPIO (light-sleep wakeup, debounce, pin mode). */
static constexpr int kButtonGpio = pins::kButton;

/** Finite blink pattern for the green header user LED (GPIO3). */
struct LedBlinkPattern {
    uint8_t  count;  // full on(+off) cycles; min 1
    uint16_t onMs;
    uint16_t offMs;
};

enum class LedPreset : uint8_t {
    Boot,      // 3× 200/200
    WifiUp,    // 2× 80/80
    MqttUp,    // 1× 150/0
    LinkDown,  // 4× 50/50
};

void buttonInit();
/** ISR + FreeRTOS task for debounce, LED, publish; call after buttonInit(). */
void buttonStartTask();
void buttonStartupBlink();
/** After the startup blink: hold the LED level in light sleep (fewer glitches). */
void buttonEnableLedGpioHoldForLightSleep();
/** Force the header LED off when the user disabled it in settings. */
void buttonApplyLedEnabled();

/** True while the non-blocking MQTT transmit LED sequence is active (for adaptive light sleep). */
bool buttonIsLedTxSequenceActive();

/** Pulse GPIO3 during E-Ink refresh / RX ack. Safe from any task. */
void ledRefreshPulseBegin();
void ledRefreshPulseEnd();
void ledRefreshPulseEndAfter(unsigned long durationMs);

/**
 * Queue a finite blink pattern (thread-safe).
 * Replaces a pending/running pattern; does not interrupt an MQTT TX sequence.
 * Priority: TX > pattern > refresh pulse.
 */
void ledPlayPattern(LedBlinkPattern pattern);
void ledPlayPreset(LedPreset preset);
