#pragma once

#include <cstdint>

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

/** Coalesce a client teardown for the network task (safe during EPD refresh). */
void mqttRequestKillClientDeferred();

void mqttPostponeConnect(unsigned long delayMs);

/** Block publishes while broker settings are torn down/reapplied. */
void mqttBeginSettingsApply();
void mqttEndSettingsApply();

bool mqttIsConnected();

bool mqttPublishChayaAndApplySentCounters();

/**
 * Non-blocking heart publish for the LED TX sequence.
 * LED/button task requests work; network task starts QoS-1 publish; PUBACK completes async.
 */
enum class MqttChayaPublishAsync : uint8_t {
    Idle,
    Pending,
    Ok,
    Fail,
};

/** Arm network-task publish if idle; returns current async state after arming. */
MqttChayaPublishAsync mqttRequestChayaPublishAsync();
MqttChayaPublishAsync mqttPollChayaPublishAsync();
void mqttRunChayaPublishOnNetworkTask();
void mqttClearChayaPublishAsync();

/** True while broker settings are being torn down/reapplied (blocks publish). */
bool mqttPublishBlocked();

/** Result of requesting a heart/Chaya TX (button and web share this entry). */
enum class ChayaSendResult : uint8_t {
    Started,      // LED TX sequence armed; publish runs on network task (STAB-02)
    Unavailable,  // shutdown, SoftAP, or broker not configured
    Busy,         // TX sequence already running or publish blocked (settings apply)
};

/**
 * Single entry for Chaya send: same LED TX sequence + publish path for button and web.
 * Safe from any task.
 */
ChayaSendResult chayaRequestSend();
