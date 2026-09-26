#pragma once

#include <cstddef>
#include <cstring>
#include <span>

#include "constants.h"
#include "mqtt/config.h"
#include "mqtt/mqtt_config.h"
#include "util/format_buf.h"

/** Lowercase A-F in a hex device / pairing / partner id. */
inline void mqttNormalizeHexIdInPlace(char *id) {
    if (id == nullptr) {
        return;
    }
    const size_t len = std::strlen(id);
    for (char &c : std::span<char>{id, len}) {
        if (c >= 'A' && c <= 'F') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
}

/**
 * Publish identity: stored pairing ID when valid, otherwise own device ID.
 * Empty pairing means this device is the 1:1 default (topicPub uses own ID).
 */
[[nodiscard]] inline auto mqttEffectivePairingId(const MqttConfig &cfg, const char *ownId) -> const char * {
    if (cfg.pairingDeviceId[0] != '\0' && deviceIdSyntaxOk(cfg.pairingDeviceId)) {
        return cfg.pairingDeviceId;
    }
    return ownId;
}

/** Format per-device LWT topic `chaya2mqtt/<deviceId>/lwt` (never the shared heart topic). */
inline void mqttFormatDeviceLwtTopic(char *out, size_t outLen, const char *deviceId) {
    if (out == nullptr || outLen == 0U) {
        return;
    }
    if (deviceId != nullptr && deviceIdSyntaxOk(deviceId)) {
        static_cast<void>(formatToBuf(std::span<char>{out, outLen}, "{}{}/lwt", kMqttPairTopicPrefix, deviceId));
        return;
    }
    out[0] = '\0';
}

/**
 * Derive topicPub/topicSub from pairing + partner IDs (no MAC/NVS).
 * Empty partner clears topicSub. Invalid own/pairing ID clears topicPub.
 */
inline void mqttApplyPairingTopicsWithIds(MqttConfig *cfg, const char *ownId) {
    if (cfg == nullptr) {
        return;
    }
    const char *pubId = mqttEffectivePairingId(*cfg, ownId);
    if (pubId != nullptr && deviceIdSyntaxOk(pubId)) {
        static_cast<void>(formatToBuf(std::span<char>{cfg->topicPub}, "{}{}", kMqttPairTopicPrefix, pubId));
    } else {
        cfg->topicPub[0] = '\0';
    }
    if (cfg->partnerDeviceId[0] != '\0' && deviceIdSyntaxOk(cfg->partnerDeviceId)) {
        static_cast<void>(formatToBuf(std::span<char>{cfg->topicSub}, "{}{}", kMqttPairTopicPrefix, cfg->partnerDeviceId));
    } else {
        cfg->topicSub[0] = '\0';
    }
}

/** Lowercase A-F in pairing id; clear if invalid or equal to ownId (empty = default). */
inline void mqttSanitizePairingId(MqttConfig &cfg, const char *ownId) {
    if (cfg.pairingDeviceId[0] == '\0') {
        return;
    }
    mqttNormalizeHexIdInPlace(cfg.pairingDeviceId);
    if (!deviceIdSyntaxOk(cfg.pairingDeviceId)) {
        cfg.pairingDeviceId[0] = '\0';
        return;
    }
    if (ownId != nullptr && std::strcmp(cfg.pairingDeviceId, ownId) == 0) {
        cfg.pairingDeviceId[0] = '\0';
    }
}

/** Lowercase A-F in partner id; clear if invalid, equal to ownId, or equal to pairing id. */
inline void mqttSanitizePartnerId(MqttConfig &cfg, const char *ownId) {
    if (cfg.partnerDeviceId[0] == '\0') {
        return;
    }
    mqttNormalizeHexIdInPlace(cfg.partnerDeviceId);
    if (!deviceIdSyntaxOk(cfg.partnerDeviceId)) {
        cfg.partnerDeviceId[0] = '\0';
        return;
    }
    if (ownId != nullptr && std::strcmp(cfg.partnerDeviceId, ownId) == 0) {
        cfg.partnerDeviceId[0] = '\0';
        return;
    }
    const char *pairingId = mqttEffectivePairingId(cfg, ownId);
    if (pairingId != nullptr && std::strcmp(cfg.partnerDeviceId, pairingId) == 0) {
        cfg.partnerDeviceId[0] = '\0';
    }
}

/** Sanitize server + pairing + partner + port and apply pairing topics. */
inline void mqttSanitizeConfigAfterLoad(MqttConfig &cfg, const char *ownId) {
    if (cfg.server[0] != '\0' && !mqttServerSyntaxOk(cfg.server, sizeof(cfg.server))) {
        cfg.server[0] = '\0';
    }
    mqttSanitizePairingId(cfg, ownId);
    mqttSanitizePartnerId(cfg, ownId);
    mqttApplyPairingTopicsWithIds(&cfg, ownId);
    cfg.port = normalizeMqttPort(static_cast<int>(cfg.port));
}
