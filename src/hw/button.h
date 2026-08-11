#pragma once

#include "pins.h"

/** Button GPIO (light-sleep wakeup, debounce, pin mode). */
static constexpr int kButtonGpio = pins::kButton;

void buttonInit();
/** ISR + FreeRTOS task for debounce, LED, publish; call after buttonInit(). */
void buttonStartTask();
void buttonStartupBlink();
/** After the startup blink: hold the LED level in light sleep (fewer glitches). */
void buttonEnableLedGpioHoldForLightSleep();

/** True while the non-blocking MQTT transmit LED sequence is active (for adaptive light sleep). */
bool buttonIsLedTxSequenceActive();
