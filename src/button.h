#pragma once

/** GPIO des Tasters (Light-Sleep-Wakeup, Debounce, Pin-Mode). */
static constexpr int kButtonGpio = 2;

void buttonInit();
void buttonStartupBlink();
/** Nach Startup-Blink: LED-Pegel im Light-Sleep halten (weniger Glitches). */
void buttonEnableLedGpioHoldForLightSleep();
void buttonLoop();
void checkLEDStatus();
void buttonDebugStatus();

/** True, solange die nicht-blockierende MQTT-Sende-LED-Sequenz laeuft (fuer adaptiven Light-Sleep). */
bool buttonIsLedTxSequenceActive();
