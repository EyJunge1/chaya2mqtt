#pragma once

#include <cstdint>
#include <cstring>

#include "util/net_validate.h"
#include "wifi/wlan_config.h"

struct PackedWifiCredentials {
    uint32_t magic;
    char     ssid[kWifiSsidMaxLen];
    char     pass[kWifiPassMaxLen];
};

struct PackedWifiConfigV2 {
    uint32_t magic;
    char     ssid[kWifiSsidMaxLen];
    char     pass[kWifiPassMaxLen];
    uint8_t  mode;
    uint8_t  ip[4];
    uint8_t  gateway[4];
    uint8_t  netmask[4];
    uint8_t  dns1[4];
    uint8_t  dns2[4];
    char     ntp1[kWifiNtpHostMaxLen];
    char     ntp2[kWifiNtpHostMaxLen];
};

inline void wlanPackOctetsOrZero(const char* dotted, uint8_t out[4]) {
    if (out == nullptr) {
        return;
    }
    if (dotted == nullptr || dotted[0] == '\0' || !parseIpv4Dotted(dotted, out)) {
        out[0] = out[1] = out[2] = out[3] = 0;
    }
}

inline void wlanPackCopyOctetsToStr(const uint8_t in[4], char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return;
    }
    if (in == nullptr || ipv4IsZero(in)) {
        out[0] = '\0';
        return;
    }
    formatIpv4Octets(in, out, outLen);
}

/** Pack validated config into cfg_v2 blob (caller must validate first). */
inline void wlanPackConfigV2(const WlanConfig& cfg, PackedWifiConfigV2* out) {
    if (out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(*out));
    out->magic = kWifiCfgPackedMagic;
    std::strncpy(out->ssid, cfg.ssid, sizeof(out->ssid) - 1U);
    std::strncpy(out->pass, cfg.pass, sizeof(out->pass) - 1U);
    out->mode = static_cast<uint8_t>(cfg.mode);
    if (cfg.mode == WlanIpMode::Static) {
        wlanPackOctetsOrZero(cfg.ip, out->ip);
        wlanPackOctetsOrZero(cfg.gateway, out->gateway);
        wlanPackOctetsOrZero(cfg.netmask, out->netmask);
    }
    wlanPackOctetsOrZero(cfg.dns1, out->dns1);
    wlanPackOctetsOrZero(cfg.dns2, out->dns2);
    std::strncpy(out->ntp1, cfg.ntp1, sizeof(out->ntp1) - 1U);
    std::strncpy(out->ntp2, cfg.ntp2, sizeof(out->ntp2) - 1U);
}

/**
 * Unpack cfg_v2 into WlanConfig.
 * Invalid static fields fall back to DHCP (same as firmware NVS path).
 * @return false when magic/SSID invalid.
 */
inline bool wlanUnpackConfigV2(const PackedWifiConfigV2& pk, WlanConfig* cfg) {
    if (cfg == nullptr || pk.magic != kWifiCfgPackedMagic) {
        return false;
    }
    wlanConfigClear(cfg);
    PackedWifiConfigV2 local = pk;
    local.ssid[sizeof(local.ssid) - 1U] = '\0';
    local.pass[sizeof(local.pass) - 1U] = '\0';
    local.ntp1[sizeof(local.ntp1) - 1U] = '\0';
    local.ntp2[sizeof(local.ntp2) - 1U] = '\0';
    if (local.ssid[0] == '\0' || strnlen(local.ssid, sizeof(local.ssid)) >= sizeof(local.ssid)
        || strnlen(local.pass, sizeof(local.pass)) >= sizeof(local.pass)) {
        return false;
    }
    wlanConfigCopyStr(cfg->ssid, sizeof(cfg->ssid), local.ssid);
    wlanConfigCopyStr(cfg->pass, sizeof(cfg->pass), local.pass);
    cfg->mode = (local.mode == static_cast<uint8_t>(WlanIpMode::Static)) ? WlanIpMode::Static
                                                                         : WlanIpMode::Dhcp;
    wlanPackCopyOctetsToStr(local.ip, cfg->ip, sizeof(cfg->ip));
    wlanPackCopyOctetsToStr(local.gateway, cfg->gateway, sizeof(cfg->gateway));
    wlanPackCopyOctetsToStr(local.netmask, cfg->netmask, sizeof(cfg->netmask));
    wlanPackCopyOctetsToStr(local.dns1, cfg->dns1, sizeof(cfg->dns1));
    wlanPackCopyOctetsToStr(local.dns2, cfg->dns2, sizeof(cfg->dns2));
    wlanConfigCopyStr(cfg->ntp1, sizeof(cfg->ntp1), local.ntp1);
    wlanConfigCopyStr(cfg->ntp2, sizeof(cfg->ntp2), local.ntp2);
    if ((std::strcmp(cfg->ntp1, "pool.ntp.org") == 0
         && std::strcmp(cfg->ntp2, "time.cloudflare.com") == 0)
        || (std::strcmp(cfg->ntp1, "time.cloudflare.com") == 0
            && std::strcmp(cfg->ntp2, "pool.ntp.org") == 0)
        || (std::strcmp(cfg->ntp1, kWifiDefaultNtp1) == 0 && cfg->ntp2[0] == '\0')) {
        cfg->ntp1[0] = cfg->ntp2[0] = '\0';
    }
    if (wlanConfigValidate(cfg) != nullptr && cfg->mode == WlanIpMode::Static) {
        cfg->mode = WlanIpMode::Dhcp;
        cfg->ip[0] = cfg->gateway[0] = cfg->netmask[0] = '\0';
        if (wlanConfigValidate(cfg) != nullptr) {
            cfg->dns1[0] = cfg->dns2[0] = '\0';
        }
    }
    return cfg->ssid[0] != '\0';
}
