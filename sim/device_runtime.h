#pragma once

#include <cstring>
#include <string>

#include "fake_clock.h"
#include "fake_mqtt_transport.h"
#include "fake_network.h"
#include "fake_nvs.h"

#include "heart/counter_pure.h"
#include "mqtt/backoff.h"
#include "mqtt/config.h"
#include "mqtt/counter_payload.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/mqtt_publish_ack.h"
#include "mqtt/pairing.h"

/**
 * Headless device core used by native Unity scenarios.
 * Uses the same pure helpers as production (pairing, backoff, counter parse).
 */
class DeviceRuntime {
  public:
    explicit DeviceRuntime(const char* ownDeviceId = "a1b2c3") {
        setOwnId(ownDeviceId);
    }

    void setOwnId(const char* id) {
        ownId_.clear();
        if (id != nullptr) {
            ownId_ = id;
        }
    }

    const char* ownId() const {
        return ownId_.c_str();
    }

    FakeClock&          clock() { return clock_; }
    FakeNvs&            nvs() { return nvs_; }
    FakeNetwork&        net() { return net_; }
    FakeMqttTransport&  transport() { return transport_; }
    const MqttConfig&   mqtt() const { return mqtt_; }
    const MqttBackoffState& backoff() const { return backoff_; }
    bool                mqttConnected() const { return transport_.connected; }
    long                remoteCounter() const { return remoteCounter_; }
    int                 localTxCounter() const { return localTxCounter_; }
    int                 displayRxDelta() const {
        return heartCounterDeltaPure(static_cast<int>(remoteCounter_), rxBaseline_);
    }
    int                 displayTxDelta() const {
        return heartCounterDeltaPure(localTxCounter_, txBaseline_);
    }

    void configureBroker(const char* server, int port, const char* user, const char* pass) {
        std::strncpy(mqtt_.server, server ? server : "", sizeof(mqtt_.server) - 1U);
        mqtt_.server[sizeof(mqtt_.server) - 1U] = '\0';
        mqtt_.port = normalizeMqttPort(port);
        std::strncpy(mqtt_.username, user ? user : "", sizeof(mqtt_.username) - 1U);
        mqtt_.username[sizeof(mqtt_.username) - 1U] = '\0';
        std::strncpy(mqtt_.password, pass ? pass : "", sizeof(mqtt_.password) - 1U);
        mqtt_.password[sizeof(mqtt_.password) - 1U] = '\0';
        mqttSanitizeConfigAfterLoad(mqtt_, ownId_.c_str());
    }

    bool pair(const char* partnerId) {
        if (partnerId == nullptr) {
            mqtt_.partnerDeviceId[0] = '\0';
        } else {
            std::strncpy(mqtt_.partnerDeviceId, partnerId, sizeof(mqtt_.partnerDeviceId) - 1U);
            mqtt_.partnerDeviceId[sizeof(mqtt_.partnerDeviceId) - 1U] = '\0';
        }
        mqttSanitizeConfigAfterLoad(mqtt_, ownId_.c_str());
        return mqtt_.partnerDeviceId[0] != '\0' || partnerId == nullptr || partnerId[0] == '\0';
    }

    void unpair() {
        mqtt_.partnerDeviceId[0] = '\0';
        mqttApplyPairingTopicsWithIds(&mqtt_, ownId_.c_str());
    }

    bool persist() {
        return nvs_.saveMqtt(mqtt_, ownId_.c_str());
    }

    bool restore() {
        MqttConfig loaded{};
        if (!nvs_.loadMqtt(&loaded, ownId_.c_str())) {
            return false;
        }
        mqtt_ = loaded;
        return true;
    }

    /** Advance simulated time and attempt connect/reconnect logic once. */
    void tick(unsigned long nowMs) {
        clock_.set(nowMs);
        tickOnce();
    }

    /** Advance clock by deltaMs then run one reconnect attempt. */
    void advance(unsigned long deltaMs) {
        clock_.advance(deltaMs);
        tickOnce();
    }

    bool publishCounter(long value) {
        if (!transport_.connected || mqtt_.topicPub[0] == '\0') {
            return false;
        }
        if (mqttPublishAckIsPending(publishAck_)) {
            return false;
        }
        const std::string payload = std::to_string(value);
        const int messageId = transport_.publish(mqtt_.topicPub, payload.c_str());
        if (messageId < 0) {
            return false;
        }
        return mqttPublishAckBegin(&publishAck_, messageId, clientGeneration_,
                                   heartSentCounterNextPure(localTxCounter_));
    }

    bool confirmPublish(int messageId) {
        if (!mqttPublishAckConfirm(&publishAck_, messageId, clientGeneration_)) {
            return false;
        }
        localTxCounter_ = heartSentCounterNextPure(localTxCounter_);
        return true;
    }

    bool confirmPendingPublish() {
        return confirmPublish(publishAck_.messageId);
    }

    bool publishPending() const {
        return mqttPublishAckIsPending(publishAck_);
    }

    bool injectRemoteCounter(const char* payload) {
        if (payload == nullptr) {
            return false;
        }
        long parsed = 0;
        if (!mqttParseCounterPayload(payload, static_cast<unsigned>(std::strlen(payload)),
                                     &parsed)) {
            return false;
        }
        remoteCounter_ = parsed;
        return true;
    }

    void forceDisconnect(bool wifiSuspect) {
        static_cast<void>(mqttPublishAckFail(&publishAck_, clientGeneration_));
        clientGeneration_++;
        transport_.disconnect();
        const unsigned long wait = mqttNextFailureBackoffMs(backoff_, wifiSuspect);
        backoff_.backoffPeriodMs = wait;
        backoff_.lastAttemptAtMs = clock_.now();
    }

  private:
    void tickOnce() {
        if (transport_.connected) {
            return;
        }
        if (!mqttBackoffElapsed(backoff_, clock_.now())) {
            return;
        }
        backoff_.lastAttemptAtMs = clock_.now();
        const unsigned long defer = mqttConnectPrecheckDeferMsPure(
            mqtt_.server[0] != '\0', net_.wifiConnected, net_.wifiStable, net_.ntpSynced);
        if (defer != 0U) {
            backoff_.backoffPeriodMs = defer;
            return;
        }
        backoff_.backoffPeriodMs = 0;
        if (!transport_.connect(mqtt_.server, mqtt_.port)) {
            const unsigned long wait =
                mqttNextFailureBackoffMs(backoff_, /*wifiSuspect=*/!net_.wifiConnected);
            backoff_.backoffPeriodMs = wait;
            backoff_.lastAttemptAtMs = clock_.now();
            return;
        }
        mqttBackoffResetOnConnect(backoff_);
        if (mqtt_.topicSub[0] != '\0') {
            static_cast<void>(transport_.subscribe(mqtt_.topicSub));
        }
    }

    std::string       ownId_;
    FakeClock         clock_{};
    FakeNvs           nvs_{};
    FakeNetwork       net_{};
    FakeMqttTransport transport_{};
    MqttConfig        mqtt_{};
    MqttBackoffState  backoff_{};
    MqttPublishAckState publishAck_{};
    uint32_t          clientGeneration_ = 1U;
    long              remoteCounter_ = 0;
    int               localTxCounter_ = 0;
    int               rxBaseline_     = 0;
    int               txBaseline_     = 0;
};
