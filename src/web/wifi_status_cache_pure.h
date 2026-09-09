#pragma once

#include "util/net_validate.h"
#include "wifi/wlan_config.h"

#include <cstdint>

/** Live or cached STA IPv4/SSID snapshot (HTTP bootstrap and SSE). */
struct WifiStaNetFields {
    bool connected = false;
    char ssid[kWifiSsidMaxLen]{};
    char ip[kIpv4StrMaxLen]{};
    char gateway[kIpv4StrMaxLen]{};
    char netmask[kIpv4StrMaxLen]{};
    char dns1[kIpv4StrMaxLen]{};
    char dns2[kIpv4StrMaxLen]{};
    int rssi = 0;
};

struct WifiStaNetCache {
    bool have = false;
    WifiStaNetFields fields{};
};

enum class WifiStaNetResolve : std::uint8_t { Fresh, Cached, None };

/**
 * Apply a live STA snapshot or fall back to last-good.
 * snapshotOk true: store live in cache (including connected=false).
 * snapshotOk false: copy cache into live when have is set.
 */
inline auto wifiStaNetResolveSnapshot(bool snapshotOk, WifiStaNetCache *cache, WifiStaNetFields *live) -> WifiStaNetResolve {
    if (cache == nullptr || live == nullptr) {
        return WifiStaNetResolve::None;
    }
    if (snapshotOk) {
        cache->have = true;
        cache->fields = *live;
        return WifiStaNetResolve::Fresh;
    }
    if (!cache->have) {
        return WifiStaNetResolve::None;
    }
    *live = cache->fields;
    return WifiStaNetResolve::Cached;
}
