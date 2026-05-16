#pragma once

void mqttSetup();
void mqttDisconnect();
void mqttLoop();

/**
 * Delay the next MQTT connect attempt by delayMs (call after mqttSetup so the web UI can finish).
 */
void mqttPostponeConnect(unsigned long delayMs);

/** True when esp_mqtt_client is connected (TLS MQTT session established). */
bool mqttIsConnected();

/** Send retained counter (heartSentCounter+1) as decimal string to mqtt_topic_pub (TLS). */
bool mqttPublishChaya();
