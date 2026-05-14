#pragma once

void displayInit();
void drawHeartWithNumber();
/** Anzeige wenn kein MQTT-Server konfiguriert (z. B. nach Flash/Reset). */
void drawSplashScreen();

/** Nach MQTT-Empfang setzen; Zeichnung in loop() mit consumeHeartRedraw() ausführen. */
void requestHeartRedraw();
bool consumeHeartRedraw();
