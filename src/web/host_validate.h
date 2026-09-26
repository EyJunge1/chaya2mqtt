#pragma once

#include <cctype>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

#include "util/format_buf.h"

/** Case-insensitive full-string host equality. */
[[nodiscard]] inline auto hostEqualsIgnoreCase(std::string_view host, std::string_view ref) -> bool {
    if (host.size() != ref.size()) {
        return false;
    }
    for (size_t i = 0; i < host.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(host[i])) != std::tolower(static_cast<unsigned char>(ref[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline auto hostEqualsIgnoreCase(const char *host, const char *ref) -> bool {
    if (host == nullptr || ref == nullptr) {
        return false;
    }
    return hostEqualsIgnoreCase(std::string_view{host}, std::string_view{ref});
}

/** True when host equals prefix (case-insensitive) or prefix followed by ':port'. */
[[nodiscard]] inline auto hostPrefixIgnoreCaseThenPortOrEnd(std::string_view host, std::string_view prefix) -> bool {
    if (host.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(host[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return host.size() == prefix.size() || host[prefix.size()] == ':';
}

[[nodiscard]] inline auto hostPrefixIgnoreCaseThenPortOrEnd(const char *host, const char *prefix) -> bool {
    if (host == nullptr || prefix == nullptr) {
        return false;
    }
    return hostPrefixIgnoreCaseThenPortOrEnd(std::string_view{host}, std::string_view{prefix});
}

/**
 * Host allow-list (pure logic).
 * @param host request host (may include :port)
 * @param apMode captive portal: setup IP and hostname allowlist only
 * @param deviceHostname current station hostname (without .local)
 * @param staIp optional STA IPv4 string; nullptr/empty skips IP match
 */
[[nodiscard]] inline auto webHostCStringAllowed(const char *host, bool apMode, const char *deviceHostname, const char *staIp)
    -> bool {
    if (host == nullptr || host[0] == '\0') {
        // HTTP/1.1 requires Host. Keep hostless HTTP/1.0 captive probes working only in AP mode.
        return apMode;
    }
    const std::string_view hostView{host};
    if (apMode) {
        // SEC-04: SoftAP allowlist only — setup IP / captive hostname (not arbitrary Host).
        if (hostEqualsIgnoreCase(hostView, "4.3.2.1")) {
            return true;
        }
        if (hostPrefixIgnoreCaseThenPortOrEnd(hostView, "4.3.2.1")) {
            return true;
        }
        if (hostPrefixIgnoreCaseThenPortOrEnd(hostView, "chaya2mqtt")) {
            return true;
        }
        if (hostPrefixIgnoreCaseThenPortOrEnd(hostView, "chaya2mqtt.local")) {
            return true;
        }
        return false;
    }
    if (deviceHostname == nullptr || deviceHostname[0] == '\0') {
        return false;
    }
    const std::string_view hostnameView{deviceHostname};
    if (hostPrefixIgnoreCaseThenPortOrEnd(hostView, hostnameView)) {
        return true;
    }
    char localPrefix[48];
    if (formatToBuf(std::span<char>{localPrefix}, "{}.local", deviceHostname) &&
        hostPrefixIgnoreCaseThenPortOrEnd(hostView, std::string_view{localPrefix})) {
        return true;
    }
    if (staIp != nullptr && staIp[0] != '\0') {
        const std::string_view ipView{staIp};
        if (hostEqualsIgnoreCase(hostView, ipView)) {
            return true;
        }
        if (hostView.starts_with(ipView) && hostView.size() > ipView.size() && hostView[ipView.size()] == ':') {
            return true;
        }
    }
    return false;
}
