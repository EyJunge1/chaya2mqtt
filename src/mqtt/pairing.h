#pragma once

#include <cstdio>
#include <cstring>

#include "constants.h"
#include "mqtt/config.h"
#include "mqtt/mqtt_config.h"

/**
 * Derive topicPub/topicSub from explicit own + partner IDs (no MAC/NVS).
 * Empty partner clears topicSub. Invalid own ID clears topicPub.
 */
inline void mqttApplyPairingTopicsWithIds(MqttConfig* cfg, const char* ownId) {
    if (cfg == nullptr) {
        return;
    }
    if (ownId != nullptr && deviceIdSyntaxOk(ownId)) {
        static_cast<void>(
            std::snprintf(cfg->topicPub, sizeof(cfg->topicPub), "%s%s", kMqttPairTopicPrefix, ownId));
    } else {
        cfg->topicPub[0] = '\0';
    }
    if (cfg->partnerDeviceId[0] != '\0' && deviceIdSyntaxOk(cfg->partnerDeviceId)) {
        static_cast<void>(std::snprintf(cfg->topicSub, sizeof(cfg->topicSub), "%s%s",
                                        kMqttPairTopicPrefix, cfg->partnerDeviceId));
    } else {
        cfg->topicSub[0] = '\0';
    }
}

/** Lowercase A-F in partner id; clear if invalid or equal to ownId. */
inline void mqttSanitizePartnerId(MqttConfig& cfg, const char* ownId) {
    if (cfg.partnerDeviceId[0] == '\0') {
        return;
    }
    for (char* p = cfg.partnerDeviceId; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'F') {
            *p = static_cast<char>(*p - 'A' + 'a');
        }
    }
    if (!deviceIdSyntaxOk(cfg.partnerDeviceId)) {
        cfg.partnerDeviceId[0] = '\0';
        return;
    }
    if (ownId != nullptr && std::strcmp(cfg.partnerDeviceId, ownId) == 0) {
        cfg.partnerDeviceId[0] = '\0';
    }
}

/** Sanitize server + partner + port and apply pairing topics. */
inline void mqttSanitizeConfigAfterLoad(MqttConfig& cfg, const char* ownId) {
    if (cfg.server[0] != '\0' && !mqttServerSyntaxOk(cfg.server, sizeof(cfg.server))) {
        cfg.server[0] = '\0';
    }
    mqttSanitizePartnerId(cfg, ownId);
    mqttApplyPairingTopicsWithIds(&cfg, ownId);
    cfg.port = normalizeMqttPort(static_cast<int>(cfg.port));
}
