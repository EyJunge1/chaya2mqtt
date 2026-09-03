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

inline bool mqttPublishAckBegin(MqttPublishAckState *state, int messageId, uint32_t clientGeneration, int expectedCounter) {
    if (state == nullptr || messageId < 0 || state->status == MqttPublishAckStatus::Pending) {
        return false;
    }
    state->status = MqttPublishAckStatus::Pending;
    state->messageId = messageId;
    state->clientGeneration = clientGeneration;
    state->expectedCounter = expectedCounter;
    return true;
}

inline bool mqttPublishAckConfirm(MqttPublishAckState *state, int messageId, uint32_t clientGeneration) {
    if (state == nullptr || state->status != MqttPublishAckStatus::Pending || state->messageId != messageId ||
        state->clientGeneration != clientGeneration) {
        return false;
    }
    state->status = MqttPublishAckStatus::Acked;
    return true;
}

inline bool mqttPublishAckFail(MqttPublishAckState *state, uint32_t clientGeneration) {
    if (state == nullptr || state->status != MqttPublishAckStatus::Pending || state->clientGeneration != clientGeneration) {
        return false;
    }
    state->status = MqttPublishAckStatus::Failed;
    return true;
}

inline bool mqttPublishAckIsPending(const MqttPublishAckState &state) { return state.status == MqttPublishAckStatus::Pending; }

inline bool mqttPublishAckWasConfirmed(const MqttPublishAckState &state, int messageId, uint32_t clientGeneration) {
    return state.status == MqttPublishAckStatus::Acked && state.messageId == messageId &&
           state.clientGeneration == clientGeneration;
}
