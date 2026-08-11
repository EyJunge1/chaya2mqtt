#pragma once

#include <cstdint>

void displayHwInitPins();

void displayInit();
/** Start dedicated FreeRTOS task (SPI/EPD); call after displayInit() once asyncInfraInit() ran. */
void displayStartTask();

void drawHeartWithNumber();
/** Screen shown when no MQTT server is configured (e.g. after flashing/reset). */
void drawSplashScreen();

/** Queue heart redraw on display task (e.g. from main loop / button). */
void requestHeartRedraw();

/** Same as requestHeartRedraw but never blocks (e.g. MQTT client callback). */
void requestHeartRedrawNonBlocking();

/**
 * Defer E-Ink drawing to display task (never call draw* from AsyncWebServer handlers).
 */
void requestDeferredDrawSplashScreen();
void requestDeferredDrawHeartScreen();
