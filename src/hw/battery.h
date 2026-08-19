#pragma once

void batteryInit();
/** Average GPIO4 ADC; call from the app task (~30 s). */
void batteryPoll();

int batteryMilliVolts();
int batteryPercent();

/** Cut LiPo latch (GPIO17 LOW). Caller should flush NVS first. */
void batterySoftOff();
