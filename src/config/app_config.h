#pragma once

#include "display/view_state.h"

#include <cstddef>
#include <cstdint>

void configLoadResetPeriodFromNvs();
void configLoadUiPrefsFromNvs();

/** Counter baseline reset: 0 = off; 1–30 UTC days (default 7 if NVS missing/invalid). */
auto configGetResetPeriodDays() -> uint8_t;
auto configSetResetPeriodDays(uint8_t days) -> bool;

/** UI language: "en" or "de" (default "en"). Snapshot under mux (QUAL-07). */
void configCopyUiLang(char *out, size_t outLen);
auto configSetUiLang(const char *lang) -> bool;

/** UI theme: "system", "light", or "dark" (default "system"). Snapshot under mux (QUAL-07). */
void configCopyUiTheme(char *out, size_t outLen);
auto configSetUiTheme(const char *theme) -> bool;

/** Header user LED activity (default true = blinks on TX/RX/refresh). */
void configLoadLedFromNvs();
auto configGetLedEnabled() -> bool;
auto configSetLedEnabled(bool enabled) -> bool;

/** Last successfully painted E-Ink view (Unknown when missing/invalid). */
void configLoadDisplayViewFromNvs();
auto configGetDisplayView() -> DisplayView;
auto configSetDisplayView(DisplayView view) -> bool;
/** Mark panel contents unknown (cache + NVS) so the next draw is not skipped. */
auto configInvalidateDisplayView() -> bool;

/** Heart-click audio: per-kind enable, volume 0–100, quiet hours (local, equal = off), TX/RX Hz/ms. */
void configLoadAudioFromNvs();
auto configGetAudioTxEnabled() -> bool;
auto configSetAudioTxEnabled(bool enabled) -> bool;
auto configGetAudioRxEnabled() -> bool;
auto configSetAudioRxEnabled(bool enabled) -> bool;
auto configGetAudioTxVolume() -> uint8_t;
auto configSetAudioTxVolume(uint8_t volume) -> bool;
auto configGetAudioRxVolume() -> uint8_t;
auto configSetAudioRxVolume(uint8_t volume) -> bool;
auto configGetAudioQuietStart() -> uint8_t;
auto configGetAudioQuietEnd() -> uint8_t;
auto configSetAudioQuietHours(uint8_t startHour, uint8_t endHour) -> bool;
auto configGetAudioTxHz() -> uint16_t;
auto configGetAudioTxMs() -> uint16_t;
auto configGetAudioRxHz() -> uint16_t;
auto configGetAudioRxMs() -> uint16_t;
auto configSetAudioTones(uint16_t txHz, uint16_t txMs, uint16_t rxHz, uint16_t rxMs) -> bool;

/** Reset RAM mirrors after factory NVS clear (before reboot). */
void app_configResetRamAfterFactoryClear();
