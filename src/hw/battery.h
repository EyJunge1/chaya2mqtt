#pragma once

void batteryInit();
/** Average GPIO4 ADC; call from the app task (~30 s). */
void batteryPoll();

int batteryMilliVolts();
int batteryPercent();

/**
 * Arm active-low PWR wake, cut the LiPo latch, and enter deep sleep when USB still supplies power.
 * Caller must wait for PWR release and flush persistent state first.
 */
void batteryPowerOffAndSleep();
