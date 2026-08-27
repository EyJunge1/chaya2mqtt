#pragma once

#include "wlan_config.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

/** Max MeCard WIFI payload: prefix + escaped SSID/pass + separators + NUL. */
constexpr size_t kWifiQrPayloadMaxLen = 13U + (kWifiSsidMaxLen - 1U) * 2U + 3U
                                        + (kWifiPassMaxLen - 1U) * 2U + 2U + 1U;

/**
 * Escape a WIFI MeCard field (backslash before \\ ; , " :).
 * @return bytes written excluding NUL, or 0 on overflow / bad args.
 */
inline size_t wifiQrEscapeField(const char* in, char* out, size_t outLen) {
    if (in == nullptr || out == nullptr || outLen == 0U) {
        return 0;
    }
    size_t o = 0;
    for (const char* p = in; *p != '\0'; ++p) {
        const char c = *p;
        const bool esc = (c == '\\' || c == ';' || c == ',' || c == '"' || c == ':');
        const size_t need = esc ? 2U : 1U;
        if (o + need >= outLen) {
            out[0] = '\0';
            return 0;
        }
        if (esc) {
            out[o++] = '\\';
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return o;
}

/**
 * Build native camera WIFI QR payload: WIFI:T:WPA;S:<ssid>;P:<pass>;;
 * Compatible with iOS/Android camera join prompts (T:WPA, not SAE-only).
 */
inline bool wifiQrBuildWpaPayload(const char* ssid, const char* pass, char* out, size_t outLen) {
    if (ssid == nullptr || ssid[0] == '\0' || pass == nullptr || out == nullptr || outLen == 0U) {
        return false;
    }
    char escSsid[(kWifiSsidMaxLen - 1U) * 2U + 1U]{};
    char escPass[(kWifiPassMaxLen - 1U) * 2U + 1U]{};
    if (wifiQrEscapeField(ssid, escSsid, sizeof(escSsid)) == 0U && ssid[0] != '\0') {
        return false;
    }
    if (pass[0] != '\0' && wifiQrEscapeField(pass, escPass, sizeof(escPass)) == 0U) {
        return false;
    }
    if (pass[0] == '\0') {
        escPass[0] = '\0';
    }
    const int n = std::snprintf(out, outLen, "WIFI:T:WPA;S:%s;P:%s;;", escSsid, escPass);
    return n > 0 && static_cast<size_t>(n) < outLen;
}
