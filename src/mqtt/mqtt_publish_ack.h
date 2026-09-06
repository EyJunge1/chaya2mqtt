#pragma once

#include <cstdint>

enum class MqttPublishAckStatus : uint8_t {
    Idle,
    Pending,
    Acked,
    Failed,
};

struct MqttPublishAckState {
    MqttPublishAckStatus status = MqttPublishAckStatus::Idle;
    int messageId = -1;
    uint32_t clientGeneration = 0;
    int expectedCounter = 0;
};

inline auto mqttPublishAckBegin(MqttPublishAckState *state, int messageId, uint32_t clientGeneration,
                                int expectedCounter) -> bool {
    if (state == nullptr || messageId < 0 || state->status == MqttPublishAckStatus::Pending) {
        return false;
    }
    state->status = MqttPublishAckStatus::Pending;
    state->messageId = messageId;
    state->clientGeneration = clientGeneration;
    state->expectedCounter = expectedCounter;
    return true;
}

inline auto mqttPublishAckConfirm(MqttPublishAckState *state, int messageId, uint32_t clientGeneration) -> bool {
    if (state == nullptr || state->status != MqttPublishAckStatus::Pending || state->messageId != messageId ||
        state->clientGeneration != clientGeneration) {
        return false;
    }
    state->status = MqttPublishAckStatus::Acked;
    return true;
}

inline auto mqttPublishAckFail(MqttPublishAckState *state, uint32_t clientGeneration) -> bool {
    if (state == nullptr || state->status != MqttPublishAckStatus::Pending || state->clientGeneration != clientGeneration) {
        return false;
    }
    state->status = MqttPublishAckStatus::Failed;
    return true;
}

inline auto mqttPublishAckIsPending(const MqttPublishAckState &state) -> bool {
    return state.status == MqttPublishAckStatus::Pending;
}

inline auto mqttPublishAckWasConfirmed(const MqttPublishAckState &state, int messageId, uint32_t clientGeneration) -> bool {
    return state.status == MqttPublishAckStatus::Acked && state.messageId == messageId &&
           state.clientGeneration == clientGeneration;
}

/** Pending + armed timer; startMs==0 is a valid millis() snapshot (not a sentinel). */
inline auto mqttPublishAckTimeoutDue(bool pending, bool timerArmed, unsigned long startedMs, unsigned long nowMs,
                                     unsigned long waitMs) -> bool {
    if (!pending || !timerArmed) {
        return false;
    }
    return (nowMs - startedMs) >= waitMs;
}
