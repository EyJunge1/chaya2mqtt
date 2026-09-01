#pragma once

#include "display/view_state.h"

#include <cstddef>
#include <cstdint>

void configLoadResetPeriodFromNvs();
void configLoadUiPrefsFromNvs();

/** Counter baseline reset: 0 = off; 1–30 UTC days (default 7 if NVS missing/invalid). */
uint8_t configGetResetPeriodDays();
bool configSetResetPeriodDays(uint8_t days);

/** UI language: "en" or "de" (default "en"). Snapshot under mux (QUAL-07). */
void configCopyUiLang(char *out, size_t outLen);
bool configSetUiLang(const char *lang);

/** UI theme: "system", "light", or "dark" (default "system"). Snapshot under mux (QUAL-07). */
void configCopyUiTheme(char *out, size_t outLen);
bool configSetUiTheme(const char *theme);

/** Header user LED activity (default true = blinks on TX/RX/refresh). */
void configLoadLedFromNvs();
bool configGetLedEnabled();
bool configSetLedEnabled(bool enabled);

/** Last successfully painted E-Ink view (Unknown when missing/invalid). */
void configLoadDisplayViewFromNvs();
DisplayView configGetDisplayView();
bool configSetDisplayView(DisplayView view);
/** Mark panel contents unknown (cache + NVS) so the next draw is not skipped. */
bool configInvalidateDisplayView();

/** Heart-click audio: per-kind enable, volume 0–100, quiet hours (local, equal = off), TX/RX Hz/ms. */
void configLoadAudioFromNvs();
bool configGetAudioTxEnabled();
bool configSetAudioTxEnabled(bool enabled);
bool configGetAudioRxEnabled();
bool configSetAudioRxEnabled(bool enabled);
uint8_t configGetAudioTxVolume();
bool configSetAudioTxVolume(uint8_t volume);
uint8_t configGetAudioRxVolume();
bool configSetAudioRxVolume(uint8_t volume);
uint8_t configGetAudioQuietStart();
uint8_t configGetAudioQuietEnd();
bool configSetAudioQuietHours(uint8_t startHour, uint8_t endHour);
uint16_t configGetAudioTxHz();
uint16_t configGetAudioTxMs();
uint16_t configGetAudioRxHz();
uint16_t configGetAudioRxMs();
bool configSetAudioTones(uint16_t txHz, uint16_t txMs, uint16_t rxHz, uint16_t rxMs);

/** Reset RAM mirrors after factory NVS clear (before reboot). */
void app_configResetRamAfterFactoryClear();
