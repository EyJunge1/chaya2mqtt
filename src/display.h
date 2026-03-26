#pragma once

void displayInit();
void drawHeartWithNumber();

/** Nach MQTT-Empfang setzen; Zeichnung in loop() mit consumeHeartRedraw() ausführen. */
void requestHeartRedraw();
bool consumeHeartRedraw();
