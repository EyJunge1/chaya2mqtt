#pragma once

#include "async/event_types.h"
#include "display_link_pure.h"
#include "heart/counter.h"
#include "view_state.h"

#include <cstdint>

void displayHwInitPins();

void displayInit();
/** Start dedicated FreeRTOS task (SPI/EPD); call after displayInit() once asyncInfraInit() ran. */
void displayStartTask();

/** Paint heart, battery, and counters; returns painted counters. */
HeartCounterDrawSnapshot drawHeartWithNumber(DisplayHeartIcon icon);
/** Setup splash: full-screen WIFI QR in SoftAP mode (phone camera join). */
DisplayView drawSplashScreen();
/** View variant that drawSplashScreen will normally produce for the current network mode. */
DisplayView displaySplashTargetView();
/** Power-off screen: centered red Lucide heart-off. */
void drawPowerOffScreen();

/** Desired Lucide heart glyph for the next STA heart paint (filled vs crack). */
void displaySetDesiredHeartIcon(DisplayHeartIcon icon);
DisplayHeartIcon displayDesiredHeartIcon();

/**
 * How to queue a display command (LED-style single entry for callers).
 * Heart vs crack stays via displaySetDesiredHeartIcon; splash picks SetupQr vs ProductTitle.
 */
enum class DisplayRequestMode : uint8_t {
    /** Heart content redraw (coalesce / min-interval). waitMs: queue wait; 0 = non-blocking. */
    Content,
    /** Splash or heart after boot/setup; skip when NVS view already matches. */
    BootIfChanged,
    /** Stop normal redraws, paint power-off, wait for that refresh (waitMs = timeout). */
    PowerOffWait,
};

/**
 * Single entry for display commands. Returns false when the command could not be queued
 * or (PowerOffWait) the refresh timed out. Content/Boot on SoftAP: Heart is a no-op (true).
 */
bool displayRequest(DisplayMsg::Cmd cmd, DisplayRequestMode mode, uint32_t waitMs = 100U);

/** Wait until the display task finishes the next queued draw (or times out). */
bool displayWaitDrawIdle(uint32_t timeoutMs);
