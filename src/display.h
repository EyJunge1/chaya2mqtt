#pragma once

#include <cstdint>

void displayInit();
void drawHeartWithNumber();
/** Anzeige wenn kein MQTT-Server konfiguriert (z. B. nach Flash/Reset). */
void drawSplashScreen();

/** Nach MQTT-Empfang setzen; Zeichnung in loop() mit consumeHeartRedraw() ausführen. */
void requestHeartRedraw();
bool consumeHeartRedraw();

/** Six-digit pairing code for web UI (E-Ink), code only centered. */
void drawAuthCode(uint32_t code);

/** Prompt shown while waiting for device button confirmation (E-Ink). */
void drawAuthPrompt();

/**
 * Defer E-Ink drawing to the Arduino main task only (never call from AsyncWebServer handlers).
 * Process with displayProcessDeferredDrawsOnMainTask() from loop().
 */
void requestDeferredDrawAuthCode(uint32_t code);
void requestDeferredDrawAuthPrompt();
void requestDeferredDrawSplashScreen();
void requestDeferredDrawHeartScreen();
void displayProcessDeferredDrawsOnMainTask();
