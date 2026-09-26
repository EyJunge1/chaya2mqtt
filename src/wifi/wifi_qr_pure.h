#pragma once

#include "wlan_config.h"

#include "util/format_buf.h"

#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

/** Max MeCard WIFI payload: prefix + escaped SSID/pass + separators + NUL. */
constexpr size_t kWifiQrPayloadMaxLen = 13U + (kWifiSsidMaxLen - 1U) * 2U + 3U + (kWifiPassMaxLen - 1U) * 2U + 2U + 1U;

/**
 * Escape a WIFI MeCard field (backslash before \\ ; , " :).
 * @return bytes written excluding NUL, or 0 on overflow / bad args.
 */
[[nodiscard]] inline auto wifiQrEscapeField(std::string_view in, std::span<char> out) -> size_t {
    if (out.empty()) {
        return 0;
    }
    size_t o = 0;
    for (const char c : in) {
        const bool esc = (c == '\\' || c == ';' || c == ',' || c == '"' || c == ':');
        const size_t need = esc ? 2U : 1U;
        if (o + need >= out.size()) {
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

[[nodiscard]] inline auto wifiQrEscapeField(const char *in, char *out, size_t outLen) -> size_t {
    if (in == nullptr || out == nullptr || outLen == 0U) {
        return 0;
    }
    return wifiQrEscapeField(std::string_view{in}, std::span<char>{out, outLen});
}

/**
 * Build native camera WIFI QR payload: WIFI:T:WPA;S:<ssid>;P:<pass>;;
 * Compatible with iOS/Android camera join prompts (T:WPA, not SAE-only).
 */
[[nodiscard]] inline auto wifiQrBuildWpaPayload(std::string_view ssid, std::string_view pass, std::span<char> out) -> bool {
    if (ssid.empty() || out.empty()) {
        return false;
    }
    char escSsid[(kWifiSsidMaxLen - 1U) * 2U + 1U]{};
    char escPass[(kWifiPassMaxLen - 1U) * 2U + 1U]{};
    if (wifiQrEscapeField(ssid, std::span<char>{escSsid}) == 0U && !ssid.empty()) {
        return false;
    }
    if (!pass.empty() && wifiQrEscapeField(pass, std::span<char>{escPass}) == 0U) {
        return false;
    }
    if (pass.empty()) {
        escPass[0] = '\0';
    }
    return formatToBuf(out, "WIFI:T:WPA;S:{};P:{};;", escSsid, escPass);
}

[[nodiscard]] inline auto wifiQrBuildWpaPayload(const char *ssid, const char *pass, char *out, size_t outLen) -> bool {
    if (ssid == nullptr || pass == nullptr || out == nullptr || outLen == 0U) {
        return false;
    }
    return wifiQrBuildWpaPayload(std::string_view{ssid}, std::string_view{pass}, std::span<char>{out, outLen});
}
