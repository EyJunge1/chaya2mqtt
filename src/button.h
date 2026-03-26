#pragma once

/** GPIO des Tasters (Light-Sleep-Wakeup, Debounce, Pin-Mode). */
static constexpr int kButtonGpio = 2;

void buttonInit();
void buttonStartupBlink();
void buttonLoop();
void checkLEDStatus();
void buttonDebugStatus();
