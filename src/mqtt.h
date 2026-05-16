#pragma once

void mqttSetup();
void mqttDisconnect();
void mqttLoop();

/**
 * Delay the next MQTT connect attempt by delayMs (call after mqttSetup so the web UI can finish).
 */
void mqttPostponeConnect(unsigned long delayMs);

/** True wenn PubSubClient mit dem Broker verbunden ist (vor TLS-Light-Sleep-Schutz nutzen). */
bool mqttIsConnected();

/**
 * True while a background FreeRTOS task runs the blocking TLS MQTT connect.
 * Main loop must not use PubSubClient/WiFiClientSecure in parallel; skip light sleep while true.
 */
bool mqttIsConnectInProgress();

/** Sendet retained Zaehler (heartSentCounter+1) als Dezimalstring auf mqtt_topic_pub (TLS). */
bool mqttPublishChaya();

/**
 * Millisekunden bis zum naechsten MQTT-Connect-Versuch (nicht verbunden).
 * @return 0 wenn verbunden oder Versuch faellig; sonst Rest bis Backoff-Ende.
 */
unsigned long mqttMillisUntilNextConnectAttempt();
