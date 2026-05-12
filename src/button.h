#pragma once

/** GPIO des Tasters (Light-Sleep-Wakeup, Debounce, Pin-Mode). */
static constexpr int kButtonGpio = 2;

void buttonInit();
void buttonStartupBlink();
/** Nach Startup-Blink: LED-Pegel im Light-Sleep halten (weniger Glitches). */
void buttonEnableLedGpioHoldForLightSleep();
void buttonLoop();
/** Nicht-blockierende MQTT-Sende-LED-Sequenz (State Machine). */
void buttonAdvanceLedSequence();

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
void buttonDebugStatus();
#endif

/** True, solange die nicht-blockierende MQTT-Sende-LED-Sequenz laeuft (fuer adaptiven Light-Sleep). */
bool buttonIsLedTxSequenceActive();
