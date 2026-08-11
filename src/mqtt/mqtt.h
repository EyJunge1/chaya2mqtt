#pragma once

/** ESP-IDF MQTT client + heart publish.
 *  Lock order when acquiring multiple mutexes (never reverse):
 *   1. g_chayaPublishMutex (`mqttPublishChaya*`),
 *   2. g_mqttClientMutex (allocate client / esp_mqtt_*),
 *   3. optional g_heartDebounceMutex (persist after a successful publish).
 *  Access broker configuration only through mqtt/config.h APIs (mqttCfgSnapshot,
 *  mqttCfgStorePending, …).
 */
void mqttSetup();
void mqttDisconnect();
void mqttLoop();

void mqttPostponeConnect(unsigned long delayMs);

/** Block publishes while broker settings are torn down/reapplied. */
void mqttBeginSettingsApply();
void mqttEndSettingsApply();

bool mqttIsConnected();

bool mqttPublishChaya();

bool mqttPublishChayaAndApplySentCounters();

/** True while broker settings are being torn down/reapplied (blocks publish). */
bool mqttPublishBlocked();
