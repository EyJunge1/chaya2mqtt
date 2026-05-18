#pragma once

#include <cstdint>

void configLoadResetPeriodFromNvs();

/** Counter baseline reset: 0 = off; 1–30 UTC days (default 7 if NVS missing/invalid). */
uint8_t configGetResetPeriodDays();
bool configSetResetPeriodDays(uint8_t days);

bool configGetWebAuthEnabled();
bool configSetWebAuthEnabled(bool enabled);
void configLoadWebAuthFromNvs();

/** Reset RAM mirrors after factory NVS clear (before reboot). */
void app_configResetRamAfterFactoryClear();
