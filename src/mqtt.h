#pragma once

void mqttSetup();
void mqttDisconnect();
void mqttLoop();

/** True wenn PubSubClient mit dem Broker verbunden ist (vor TLS-Light-Sleep-Schutz nutzen). */
bool mqttIsConnected();

/** Sendet einen Publish-Versuch "chaya" auf mqtt_topic_pub (TLS). */
bool mqttPublishChaya();

/**
 * Millisekunden bis zum naechsten MQTT-Connect-Versuch (nicht verbunden).
 * @return 0 wenn verbunden oder Versuch faellig; sonst Rest bis Backoff-Ende.
 */
unsigned long mqttMillisUntilNextConnectAttempt();
