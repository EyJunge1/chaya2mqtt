#pragma once

#include <cstdint>

void displayInit();
/** Start dedicated FreeRTOS task (SPI/EPD); call after displayInit() once asyncInfraInit() ran. */
void displayStartTask();

void drawHeartWithNumber();
/** Anzeige wenn kein MQTT-Server konfiguriert (z. B. nach Flash/Reset). */
void drawSplashScreen();

/** Queue heart redraw on display task (e.g. MQTT). */
void requestHeartRedraw();

/** Six-digit pairing code for web UI (E-Ink), code only centered. */
void drawAuthCode(uint32_t code);

/** Prompt shown while waiting for device button confirmation (E-Ink). */
void drawAuthPrompt();

/**
 * Defer E-Ink drawing to display task (never call draw* from AsyncWebServer handlers).
 */
void requestDeferredDrawAuthCode(uint32_t code);
void requestDeferredDrawAuthPrompt();
void requestDeferredDrawSplashScreen();
void requestDeferredDrawHeartScreen();
