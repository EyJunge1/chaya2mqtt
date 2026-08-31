#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "util/net_validate.h"

/** Wi-Fi SSID/password buffers (IEEE max + NUL). */
constexpr size_t kWifiSsidMaxLen = 33U;
constexpr size_t kWifiPassMaxLen = 65U;
constexpr size_t kWifiNtpHostMaxLen = 64U;

/** SoftAP setup PSK: alphanumeric ≥20 (WIFI QR only; no manual typing fallback). */
constexpr size_t kSetupApPassLen    = 24U;
constexpr size_t kSetupApPassBufLen = kSetupApPassLen + 1U;

inline bool setupApPassCharOk(unsigned char c) {
    return (c >= static_cast<unsigned char>('0') && c <= static_cast<unsigned char>('9'))
           || (c >= static_cast<unsigned char>('A') && c <= static_cast<unsigned char>('Z'))
           || (c >= static_cast<unsigned char>('a') && c <= static_cast<unsigned char>('z'));
}

inline bool setupApPassSyntaxOk(const char* pass) {
    if (pass == nullptr) {
        return false;
    }
    for (size_t i = 0; i < kSetupApPassLen; ++i) {
        if (!setupApPassCharOk(static_cast<unsigned char>(pass[i]))) {
            return false;
        }
    }
    return pass[kSetupApPassLen] == '\0';
}

/**
 * Fill a SoftAP PSK from raw CSPRNG bytes (maps each byte into [0-9A-Za-z]).
 * @param rnd must provide at least kSetupApPassLen bytes
 */
inline bool formatSetupApPassFromRandom(const uint8_t* rnd, size_t rndLen, char* out, size_t outLen) {
    if (rnd == nullptr || rndLen < kSetupApPassLen || out == nullptr || outLen < kSetupApPassBufLen) {
        return false;
    }
    static constexpr char kAlphabet[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    constexpr size_t kAlphabetLen = sizeof(kAlphabet) - 1U;
    for (size_t i = 0; i < kSetupApPassLen; ++i) {
        out[i] = kAlphabet[rnd[i] % kAlphabetLen];
    }
    out[kSetupApPassLen] = '\0';
    return true;
}

/** Initial STA wait before handing an offline configured device to recovery. */
constexpr unsigned long kWifiStaBootConnectTimeoutMs = 10000UL;
constexpr uint8_t       kWifiStaMaxTxPowerQuarterDbm = 52U;
constexpr uint16_t      kWifiStaInactiveTimeSeconds  = 30U;

/** EPD low-interference TX caps (ESP-IDF quarter-dBm units: value/4 = dBm). */
constexpr int8_t kWifiEpdTxPowerStrongQdbm = 8;  // 2 dBm — good link
constexpr int8_t kWifiEpdTxPowerMediumQdbm = 28; // 7 dBm — fair link
constexpr int8_t kWifiEpdTxPowerWeakQdbm   = 40; // 10 dBm — weak / unknown RSSI
constexpr int    kWifiEpdRssiStrongMinDbm  = -55;
constexpr int    kWifiEpdRssiMediumMinDbm  = -64;

/**
 * Choose the EPD TX power cap from STA RSSI. Never raises the current max.
 * `rssi <= 0` from WiFi.RSSI() when associated; 0 or positive is treated as unknown.
 */
inline int8_t wlanEpdTxPowerQuarterDbmFromRssi(int rssi, int8_t currentMaxQdbm) {
    int8_t target = kWifiEpdTxPowerWeakQdbm;
    if (rssi < 0) {
        if (rssi >= kWifiEpdRssiStrongMinDbm) {
            target = kWifiEpdTxPowerStrongQdbm;
        } else if (rssi >= kWifiEpdRssiMediumMinDbm) {
            target = kWifiEpdTxPowerMediumQdbm;
        }
    }
    return (currentMaxQdbm < target) ? currentMaxQdbm : target;
}

enum class WlanBootAction : uint8_t {
    WaitForSta      = 0,
    FinishSta       = 1,
    ContinueStaOnly = 2,
    StartSetupAp    = 3,
};

/**
 * Pure boot decision for unit tests.
 * Once STA credentials exist, a timeout must never expose the setup AP again.
 */
inline WlanBootAction wlanBootDecide(bool hasStaCredentials, bool staConnected,
                                     bool staConnectTimedOut) {
    if (!hasStaCredentials) {
        return WlanBootAction::StartSetupAp;
    }
    if (staConnected) {
        return WlanBootAction::FinishSta;
    }
    if (staConnectTimedOut) {
        return WlanBootAction::ContinueStaOnly;
    }
    return WlanBootAction::WaitForSta;
}

/** STA stability / scan timing (module-local). */
constexpr unsigned long kStaStableAfterGotIpMs   = 3000UL;
/** After GOT_IP, defer admin STA scans to avoid disconnect races (STAB-07). */
constexpr unsigned long kWifiScanAfterGotIpCooldownMs = 45000UL;
constexpr unsigned long kWifiScanFailBackoffMs     = 5000UL;
constexpr unsigned long kWifiReconnectBaseBackoffMs = 3000UL;
constexpr unsigned long kWifiReconnectMaxBackoffMs  = 120000UL;
/** Soft `WiFi.STA.connect()` attempts before escalating to disconnect+begin. */
constexpr uint32_t      kWifiSoftReconnectAttemptsBeforeForce = 2U;
constexpr unsigned long kApDnsPollIntervalMs        = 5000UL;

/** Legacy packed credentials blob (SSID+pass only). */
constexpr uint32_t kWifiCredPackedMagic = 0x43575631U; // "CWV1"
/** Full network config blob (SSID+pass+IPv4+NTP). */
constexpr uint32_t kWifiCfgPackedMagic  = 0x43575632U; // "CWV2"

/** Fallback DNS when none from DHCP / no override (Cloudflare). */
constexpr const char kWifiDefaultDns1[] = "1.1.1.1";
constexpr const char kWifiDefaultDns2[] = "1.0.0.1";

/** Fallback NTP when DHCP option 42 is absent (Cloudflare). */
constexpr const char kWifiDefaultNtp1[] = "time.cloudflare.com";

enum class WlanIpMode : uint8_t {
    Dhcp   = 0,
    Static = 1,
};

struct WlanConfig {
    char       ssid[kWifiSsidMaxLen];
    char       pass[kWifiPassMaxLen];
    WlanIpMode mode;
    char       ip[kIpv4StrMaxLen];
    char       gateway[kIpv4StrMaxLen];
    char       netmask[kIpv4StrMaxLen];
    char       dns1[kIpv4StrMaxLen];
    char       dns2[kIpv4StrMaxLen];
    char       ntp1[kWifiNtpHostMaxLen];
    char       ntp2[kWifiNtpHostMaxLen];
};

inline void wlanConfigClear(WlanConfig* cfg) {
    if (cfg == nullptr) {
        return;
    }
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->mode = WlanIpMode::Dhcp;
}

inline void wlanConfigCopyStr(char* dst, size_t dstLen, const char* src) {
    if (dst == nullptr || dstLen == 0U) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dstLen - 1U);
    dst[dstLen - 1U] = '\0';
}

/**
 * Fill empty NTP with built-in fallback (runtime apply only — not for NVS storage).
 * Empty NTP in config means automatic: DHCP option 42 when offered, else Cloudflare.
 */
inline void wlanConfigSetNtpDefaults(WlanConfig* cfg) {
    if (cfg == nullptr) {
        return;
    }
    if (cfg->ntp1[0] == '\0') {
        wlanConfigCopyStr(cfg->ntp1, sizeof(cfg->ntp1), kWifiDefaultNtp1);
    }
    cfg->ntp2[0] = '\0';
}

/**
 * Validate WLAN config for connect/save.
 * Always: SSID; optional DNS/NTP (must be valid when set). Empty NTP = automatic
 * (DHCP NTP if offered, else time.cloudflare.com).
 * DHCP: ignores IP/gateway/netmask.
 * Static: requires IP, netmask, gateway; same-subnet + contiguous mask.
 * Returns nullptr on success, else a short error token for the API.
 */
inline const char* wlanConfigValidate(const WlanConfig* cfg) {
    if (cfg == nullptr || cfg->ssid[0] == '\0') {
        return "ssid";
    }
    if (cfg->ntp1[0] != '\0' && !ntpHostSyntaxOk(cfg->ntp1, sizeof(cfg->ntp1))) {
        return "ntp1";
    }
    if (cfg->ntp2[0] != '\0' && !ntpHostSyntaxOk(cfg->ntp2, sizeof(cfg->ntp2))) {
        return "ntp2";
    }
    if (cfg->dns1[0] != '\0') {
        uint8_t dns1[4]{};
        if (!parseIpv4Dotted(cfg->dns1, dns1) || ipv4IsZero(dns1)) {
            return "dns1";
        }
    }
    if (cfg->dns2[0] != '\0') {
        uint8_t dns2[4]{};
        if (!parseIpv4Dotted(cfg->dns2, dns2) || ipv4IsZero(dns2)) {
            return "dns2";
        }
    }
    if (cfg->mode == WlanIpMode::Dhcp) {
        return nullptr;
    }
    if (cfg->mode != WlanIpMode::Static) {
        return "mode";
    }
    uint8_t ip[4]{};
    uint8_t gw[4]{};
    uint8_t mask[4]{};
    if (!parseIpv4Dotted(cfg->ip, ip) || ipv4IsZero(ip)) {
        return "ip";
    }
    if (!parseIpv4Dotted(cfg->gateway, gw) || ipv4IsZero(gw)) {
        return "gateway";
    }
    if (!parseIpv4Dotted(cfg->netmask, mask) || !ipv4NetmaskContiguousOk(mask)) {
        return "netmask";
    }
    if (!ipv4SameSubnet(ip, gw, mask)) {
        return "subnet";
    }
    return nullptr;
}
