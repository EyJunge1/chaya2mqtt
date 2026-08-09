#pragma once

#include <cstdint>

void configLoadResetPeriodFromNvs();
void configLoadUiPrefsFromNvs();

/** Counter baseline reset: 0 = off; 1–30 UTC days (default 7 if NVS missing/invalid). */
uint8_t configGetResetPeriodDays();
bool configSetResetPeriodDays(uint8_t days);

/** UI language: "en" or "de" (default "en"). */
const char* configGetUiLang();
bool configSetUiLang(const char* lang);

/** UI theme: "light" or "dark" (default "light"). */
const char* configGetUiTheme();
bool configSetUiTheme(const char* theme);

/** Reset RAM mirrors after factory NVS clear (before reboot). */
void app_configResetRamAfterFactoryClear();
