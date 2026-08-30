#pragma once

#include "config.h"
#include "mqtt_timing.h"

#include <atomic>
#include <cstdint>

#include <freertos/portmacro.h>
#include <mqtt_client.h>

extern esp_mqtt_client_handle_t s_client;
extern std::atomic<uint32_t>      s_clientGeneration;
extern std::atomic<bool>          s_connected;
extern std::atomic<bool>          s_connectPending;
extern std::atomic<bool>          s_disconnectIntentional;
extern std::atomic<bool>          s_mqttKillCoalesce;

extern unsigned long lastMqttAttemptAt;
extern unsigned long mqttBackoffMs;
extern unsigned long mqttCurrentBackoffMs;
extern portMUX_TYPE  s_mqttBackoffMux;

extern char           s_clientIdBuf[24];
extern char           s_lwtTopicBuf[sizeof(MqttConfig::topicPub) + 16U];
extern char           s_mqttSubTopicCache[sizeof(MqttConfig::topicSub)];
extern size_t         s_mqttSubTopicLen;
extern portMUX_TYPE   s_mqttSubTopicMux;

bool mqttClientLockTimed();
/** Fail-closed timed lock (same timeout as mqttClientLockTimed). */
bool mqttClientLock();
void mqttClientUnlock();

void mqttKillClientImpl();
void mqttKillClient();

bool mqttEnsureClientAllocated();

void mqttResetFragmentState();
void mqttHandlePublishedAck(int messageId, uint32_t clientGeneration);
void mqttAbortPendingPublish(uint32_t clientGeneration);
/** Fail pending PUBACK wait after kMqttPublishAckWaitMs (call from mqttLoop). */
void mqttServicePublishAckTimeout();

void applyDisconnectFailureBackoff(bool wifiSuspectDuringFailure);

unsigned long mqttConnectPrecheckDeferMs();

void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id,
                      void* event_data);
