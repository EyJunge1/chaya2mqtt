#pragma once

#include "display/view_state.h"

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

/** Header user LED activity (default true = blinks on TX/RX/refresh). */
void configLoadLedFromNvs();
bool configGetLedEnabled();
bool configSetLedEnabled(bool enabled);

/** Last successfully painted E-Ink view (Unknown when missing/invalid). */
void        configLoadDisplayViewFromNvs();
DisplayView configGetDisplayView();
bool        configSetDisplayView(DisplayView view);
/** Mark panel contents unknown (cache + NVS) so the next draw is not skipped. */
bool        configInvalidateDisplayView();

/** Heart-click audio: mute, volume 0–100, quiet hours (local, equal = off), optional custom tone. */
void configLoadAudioFromNvs();
bool configGetAudioMuted();
bool configSetAudioMuted(bool muted);
uint8_t configGetAudioVolume();
bool configSetAudioVolume(uint8_t volume);
uint8_t configGetAudioQuietStart();
uint8_t configGetAudioQuietEnd();
bool configSetAudioQuietHours(uint8_t startHour, uint8_t endHour);
/** When true, play configured TX/RX Hz/ms; otherwise built-in default tone. */
bool configGetAudioCustom();
bool configSetAudioCustom(bool enabled);
uint16_t configGetAudioTxHz();
uint16_t configGetAudioTxMs();
uint16_t configGetAudioRxHz();
uint16_t configGetAudioRxMs();
bool configSetAudioTones(uint16_t txHz, uint16_t txMs, uint16_t rxHz, uint16_t rxMs);

/** Reset RAM mirrors after factory NVS clear (before reboot). */
void app_configResetRamAfterFactoryClear();
