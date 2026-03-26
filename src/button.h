#pragma once

/** GPIO des Tasters (Light-Sleep-Wakeup, Debounce, Pin-Mode). */
inline constexpr int kButtonGpio = 2;

void buttonInit();
void buttonStartupBlink();
void buttonLoop();
void checkLEDStatus();
void buttonDebugStatus();

/** True, solange die nicht-blockierende MQTT-Sende-LED-Sequenz laeuft (fuer adaptiven Light-Sleep). */
bool buttonIsLedTxSequenceActive();
