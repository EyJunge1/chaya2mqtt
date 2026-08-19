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

/** E-Ink display dark mode (default false = white background). */
void configLoadDisplayDarkFromNvs();
bool configGetDisplayDark();
bool configSetDisplayDark(bool dark);

/** Heart-click audio: mute, volume 0–100, quiet hours (local, equal = off). */
void configLoadAudioFromNvs();
bool configGetAudioMuted();
bool configSetAudioMuted(bool muted);
uint8_t configGetAudioVolume();
bool configSetAudioVolume(uint8_t volume);
uint8_t configGetAudioQuietStart();
uint8_t configGetAudioQuietEnd();
bool configSetAudioQuietHours(uint8_t startHour, uint8_t endHour);

/** Reset RAM mirrors after factory NVS clear (before reboot). */
void app_configResetRamAfterFactoryClear();
