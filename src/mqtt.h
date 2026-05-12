#pragma once

void mqttSetup();
void mqttDisconnect();
void mqttLoop();

/** Sendet einen Publish-Versuch "heart" auf mqtt_topic_pub (TLS). */
bool mqttPublishHeart();

/**
 * Millisekunden bis zum naechsten MQTT-Connect-Versuch (nicht verbunden).
 * @return 0 wenn verbunden oder Versuch faellig; sonst Rest bis Backoff-Ende.
 */
unsigned long mqttMillisUntilNextConnectAttempt();
