#pragma once

#include <cstdint>

void displayInit();
void drawHeartWithNumber();
/** Anzeige wenn kein MQTT-Server konfiguriert (z. B. nach Flash/Reset). */
void drawSplashScreen();

/** Nach MQTT-Empfang setzen; Zeichnung in loop() mit consumeHeartRedraw() ausführen. */
void requestHeartRedraw();
bool consumeHeartRedraw();

/** Six-digit pairing code for web UI (E-Ink). */
void drawAuthCode(uint32_t code);

/**
 * Defer E-Ink drawing to the Arduino main task only (never call from AsyncWebServer handlers).
 * Process with displayProcessDeferredDrawsOnMainTask() from loop().
 */
void requestDeferredDrawAuthCode(uint32_t code);
void requestDeferredDrawSplashScreen();
void requestDeferredDrawHeartScreen();
void displayProcessDeferredDrawsOnMainTask();
