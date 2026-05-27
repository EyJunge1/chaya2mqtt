#pragma once

/** ESP-IDF MQTT client + Herz-Publish.
 *  Lock-Reihenfolge bei mehreren Mutexen (niemals umkehren):
 *   1. g_chayaPublishMutex (`mqttPublishChaya*`),
 *   2. g_mqttClientMutex (Client allozieren / esp_mqtt_*),
 *   3. optional g_heartDebounceMutex (Persist nach erfolgreichem Publish).
 *  Broker-Konfiguration nur ueber mqtt/config.h APIs (mqttCfgSnapshot, mqttCfgStorePending, …).
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
