#pragma once

#include "heart/counter.h"
#include "view_state.h"

#include <cstdint>

void displayHwInitPins();

void displayInit();
/** Start dedicated FreeRTOS task (SPI/EPD); call after displayInit() once asyncInfraInit() ran. */
void displayStartTask();

/** Paint heart + counters; returns the snapshot that was actually drawn. */
HeartCounterDrawSnapshot drawHeartWithNumber();
/** Setup splash: full-screen WIFI QR in SoftAP mode (phone camera join). */
DisplayView drawSplashScreen();
/** View variant that drawSplashScreen will normally produce for the current network mode. */
DisplayView displaySplashTargetView();
/** Power-off screen: centered red product name, independent of setup/AP state. */
void drawPowerOffScreen();

/** Queue heart redraw on display task (e.g. from main loop / button). */
void requestHeartRedraw();

/** Same as requestHeartRedraw but never blocks (e.g. MQTT client callback). */
bool requestHeartRedrawNonBlocking();

/**
 * Defer E-Ink drawing to display task (never call draw* from AsyncWebServer handlers).
 */
void requestDeferredDrawSplashScreen();
void requestDeferredDrawHeartScreen();

/** Wait until the display task finishes the next queued draw (or times out). */
bool displayWaitDrawIdle(uint32_t timeoutMs);

/**
 * Stop accepting normal redraws, draw the power-off screen, and wait for that exact refresh.
 * Returns false when the command cannot be queued or the refresh times out.
 */
bool displayDrawPowerOffAndWait(uint32_t timeoutMs);