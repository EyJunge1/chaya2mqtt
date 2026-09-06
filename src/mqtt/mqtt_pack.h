#pragma once

#include <cstdint>
#include <cstring>

#include "config.h"

constexpr uint32_t kMqttCfgPackedMagic = 0x434D5631U; // "CMV1"

struct PackedMqttConfigV1 {
    uint32_t magic;
    char server[sizeof(MqttConfig::server)];
    uint16_t port;
    uint8_t tls;
    char username[sizeof(MqttConfig::username)];
    char password[sizeof(MqttConfig::password)];
    char partnerDeviceId[sizeof(MqttConfig::partnerDeviceId)];
} __attribute__((packed));

/** Pack persistable broker fields into cfg_v1 blob (topics are derived, not stored). */
inline void mqttPackConfigV1(const MqttConfig &cfg, PackedMqttConfigV1 *out) {
    if (out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(*out));
    out->magic = kMqttCfgPackedMagic;
    std::strncpy(out->server, cfg.server, sizeof(out->server) - 1U);
    out->port = cfg.port;
    out->tls = cfg.tls ? 1U : 0U;
    std::strncpy(out->username, cfg.username, sizeof(out->username) - 1U);
    std::strncpy(out->password, cfg.password, sizeof(out->password) - 1U);
    std::strncpy(out->partnerDeviceId, cfg.partnerDeviceId, sizeof(out->partnerDeviceId) - 1U);
}

/**
 * Unpack cfg_v1 into MqttConfig persistable fields (topics left empty).
 * @return false when magic is wrong or a string is not NUL-terminated.
 */
inline auto mqttUnpackConfigV1(const PackedMqttConfigV1 &pk, MqttConfig *cfg) -> bool {
    if (cfg == nullptr || pk.magic != kMqttCfgPackedMagic) {
        return false;
    }
    if (strnlen(pk.server, sizeof(pk.server)) >= sizeof(pk.server) ||
        strnlen(pk.username, sizeof(pk.username)) >= sizeof(pk.username) ||
        strnlen(pk.password, sizeof(pk.password)) >= sizeof(pk.password) ||
        strnlen(pk.partnerDeviceId, sizeof(pk.partnerDeviceId)) >= sizeof(pk.partnerDeviceId)) {
        return false;
    }
    *cfg = MqttConfig{};
    std::strncpy(cfg->server, pk.server, sizeof(cfg->server) - 1U);
    cfg->server[sizeof(cfg->server) - 1U] = '\0';
    cfg->port = pk.port;
    cfg->tls = pk.tls != 0U;
    std::strncpy(cfg->username, pk.username, sizeof(cfg->username) - 1U);
    cfg->username[sizeof(cfg->username) - 1U] = '\0';
    std::strncpy(cfg->password, pk.password, sizeof(cfg->password) - 1U);
    cfg->password[sizeof(cfg->password) - 1U] = '\0';
    std::strncpy(cfg->partnerDeviceId, pk.partnerDeviceId, sizeof(cfg->partnerDeviceId) - 1U);
    cfg->partnerDeviceId[sizeof(cfg->partnerDeviceId) - 1U] = '\0';
    return true;
}
