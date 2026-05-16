#pragma once

#include <cstdint>

/** Load reset interval (days) from NVS into RAM (call once at boot). */
void configLoadResetPeriodFromNvs();

/** Display reset interval: 0 = periodic reset off; 1–30 = UTC calendar days (default when unset in NVS: 7). */
uint8_t configGetResetPeriodDays();
void configSetResetPeriodDays(uint8_t days);

/** Web UI access code (device display); NVS cfg/authEn, default off. */
bool configGetWebAuthEnabled();
void configSetWebAuthEnabled(bool enabled);
void configLoadWebAuthFromNvs();

/** After NVS factory clear — RAM mirrors cleared cfg before restart. */
void app_configResetRamAfterFactoryClear();
