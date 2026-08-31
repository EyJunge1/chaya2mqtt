#pragma once

void batteryInit();
/** Average GPIO4 ADC; call from the app task (~30 s). */
void batteryPoll();

int batteryMilliVolts();
int batteryPercent();

/** Drive BAT_Control LOW (cut LiPo latch). Safe to call more than once. */
void batteryCutLatch();

/**
 * Arm active-low PWR wake (EXT1 ANY_LOW) only when PWR is stably not LOW, cut the
 * LiPo latch, and enter deep sleep when USB still supplies power.
 * Caller must wait for PWR release and flush persistent state first.
 */
void batteryPowerOffAndSleep();
