#pragma once

#include "pins.h"

/** GPIO des Tasters (Light-Sleep-Wakeup, Debounce, Pin-Mode). */
static constexpr int kButtonGpio = pins::kButton;

void buttonInit();
/** ISR + FreeRTOS task for debounce, LED, publish; call after buttonInit(). */
void buttonStartTask();
void buttonStartupBlink();
/** Nach Startup-Blink: LED-Pegel im Light-Sleep halten (weniger Glitches). */
void buttonEnableLedGpioHoldForLightSleep();

/** True, solange die nicht-blockierende MQTT-Sende-LED-Sequenz laeuft (fuer adaptiven Light-Sleep). */
bool buttonIsLedTxSequenceActive();
