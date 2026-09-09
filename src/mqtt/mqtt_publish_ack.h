#pragma once

#include <cstdint>

enum class MqttPublishAckStatus : uint8_t {
    Idle,
    Starting,
    Pending,
    Acked,
    Failed,
};

struct MqttPublishAckState {
    MqttPublishAckStatus status = MqttPublishAckStatus::Idle;
    int messageId = -1;
    uint32_t clientGeneration = 0;
    int expectedCounter = 0;
    /** PUBACK arrived while Starting (before Attach bound the msg-id). */
    int lateAckMessageId = -1;
};

/** True only for Idle/Failed — a new QoS-1 publish may Reserve/Begin. */
inline auto mqttPublishAckCanBegin(const MqttPublishAckState &state) -> bool {
    return state.status == MqttPublishAckStatus::Idle || state.status == MqttPublishAckStatus::Failed;
}

/** Reserve also requires the async request still Pending (abort-without-ACK blocks Reserve). */
inline auto mqttPublishAckBeginAllowed(bool canBegin, bool asyncStillPending) -> bool {
    return canBegin && asyncStillPending;
}

inline auto mqttPublishAckIsStarting(const MqttPublishAckState &state) -> bool {
    return state.status == MqttPublishAckStatus::Starting;
}

/** Starting, Pending, or Acked: do not start another publish. */
inline auto mqttPublishAckBlocksNewPublish(const MqttPublishAckState &state) -> bool {
    return state.status == MqttPublishAckStatus::Starting || state.status == MqttPublishAckStatus::Pending ||
           state.status == MqttPublishAckStatus::Acked;
}

/** Reserve a start-slot before esp_mqtt_client_publish (BUG-MQTT-10). */
inline auto mqttPublishAckReserve(MqttPublishAckState *state, uint32_t clientGeneration, int expectedCounter) -> bool {
    if (state == nullptr || !mqttPublishAckCanBegin(*state)) {
        return false;
    }
    state->status = MqttPublishAckStatus::Starting;
    state->messageId = -1;
    state->clientGeneration = clientGeneration;
    state->expectedCounter = expectedCounter;
    state->lateAckMessageId = -1;
    return true;
}

/** Attach msg-id after publish returns pid>=0. Does not require async still Pending. */
inline auto mqttPublishAckAttach(MqttPublishAckState *state, int messageId, uint32_t clientGeneration) -> bool {
    if (state == nullptr || messageId < 0 || state->status != MqttPublishAckStatus::Starting ||
        state->clientGeneration != clientGeneration) {
        return false;
    }
    state->status = MqttPublishAckStatus::Pending;
    state->messageId = messageId;
    if (state->lateAckMessageId == messageId) {
        state->status = MqttPublishAckStatus::Acked;
        state->lateAckMessageId = -1;
    }
    return true;
}

inline auto mqttPublishAckBegin(MqttPublishAckState *state, int messageId, uint32_t clientGeneration,
                                int expectedCounter) -> bool {
    if (state == nullptr || messageId < 0 || !mqttPublishAckCanBegin(*state)) {
        return false;
    }
    state->status = MqttPublishAckStatus::Pending;
    state->messageId = messageId;
    state->clientGeneration = clientGeneration;
    state->expectedCounter = expectedCounter;
    state->lateAckMessageId = -1;
    return true;
}

inline auto mqttPublishAckConfirm(MqttPublishAckState *state, int messageId, uint32_t clientGeneration) -> bool {
    if (state == nullptr || messageId < 0 || state->clientGeneration != clientGeneration) {
        return false;
    }
    if (state->status == MqttPublishAckStatus::Starting) {
        state->lateAckMessageId = messageId;
        return false;
    }
    if (state->status != MqttPublishAckStatus::Pending || state->messageId != messageId) {
        return false;
    }
    state->status = MqttPublishAckStatus::Acked;
    state->lateAckMessageId = -1;
    return true;
}

inline auto mqttPublishAckFail(MqttPublishAckState *state, uint32_t clientGeneration) -> bool {
    if (state == nullptr || state->clientGeneration != clientGeneration) {
        return false;
    }
    if (state->status != MqttPublishAckStatus::Starting && state->status != MqttPublishAckStatus::Pending) {
        return false;
    }
    state->status = MqttPublishAckStatus::Failed;
    state->lateAckMessageId = -1;
    return true;
}

/** Disconnect/timeout: fail only a bound PUBACK wait. Starting stays so Attach can bind. */
inline auto mqttPublishAckFailIfPending(MqttPublishAckState *state, uint32_t clientGeneration) -> bool {
    if (state == nullptr || state->clientGeneration != clientGeneration ||
        state->status != MqttPublishAckStatus::Pending) {
        return false;
    }
    state->status = MqttPublishAckStatus::Failed;
    state->lateAckMessageId = -1;
    return true;
}

inline auto mqttPublishAckIsPending(const MqttPublishAckState &state) -> bool {
    return state.status == MqttPublishAckStatus::Pending;
}

/** Starting or Pending: an in-flight reservation — do not reset or treat as Idle. */
inline auto mqttPublishAckIsReserved(const MqttPublishAckState &state) -> bool {
    return state.status == MqttPublishAckStatus::Starting || state.status == MqttPublishAckStatus::Pending;
}

inline auto mqttPublishAckWasConfirmed(const MqttPublishAckState &state, int messageId, uint32_t clientGeneration) -> bool {
    return state.status == MqttPublishAckStatus::Acked && state.messageId == messageId &&
           state.clientGeneration == clientGeneration;
}

/** Extra async Pending→Fail only when no PUBACK was in flight, or Fail actually ran. */
inline auto mqttAbortMayFailAsync(bool ackPending, bool failAckSucceeded) -> bool {
    return !ackPending || failAckSucceeded;
}

/** Pending + armed timer; startMs==0 is a valid millis() snapshot (not a sentinel). */
inline auto mqttPublishAckTimeoutDue(bool pending, bool timerArmed, unsigned long startedMs, unsigned long nowMs,
                                     unsigned long waitMs) -> bool {
    if (!pending || !timerArmed) {
        return false;
    }
    return (nowMs - startedMs) >= waitMs;
}
