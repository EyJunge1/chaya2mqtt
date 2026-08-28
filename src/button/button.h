#pragma once

#include "hw/pins.h"

/** Button GPIO (light-sleep wakeup, debounce, pin mode). */
static constexpr int kButtonGpio = pins::kButton;

void buttonInit();
/** ISR + FreeRTOS task for debounce, LED drive, publish; call after buttonInit(). */
void buttonStartTask();
void buttonStartupBlink();
/** Wake the button task (e.g. from LED pattern/refresh queue). */
void buttonNotifyTask();
