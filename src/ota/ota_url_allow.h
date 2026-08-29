#pragma once

#include "version_cmp.h"

#include <cstddef>
#include <cstring>

enum class OtaDownloadAsset {
    Firmware,
    Sha256,
};

inline bool otaReleaseDownloadUrlAllowed(const char* url, OtaDownloadAsset asset) {
    constexpr const char kPrefix[] =
        "https://github.com/EyJunge1/chaya2mqtt/releases/download/";
    if (url == nullptr || strncmp(url, kPrefix, sizeof(kPrefix) - 1U) != 0) {
        return false;
    }
    const char* tagStart = url + sizeof(kPrefix) - 1U;
    const char* slash    = strchr(tagStart, '/');
    if (slash == nullptr || slash == tagStart) {
        return false;
    }
    const size_t tagLen = static_cast<size_t>(slash - tagStart);
    if (tagLen >= 64U) {
        return false;
    }
    char tag[64]{};
    memcpy(tag, tagStart, tagLen);
    tag[tagLen] = '\0';
    if (!otaReleaseTagIsAllowed(tag)) {
        return false;
    }
    const char* expected =
        asset == OtaDownloadAsset::Firmware ? "firmware.bin" : "firmware.sha256";
    return strcmp(slash + 1, expected) == 0;
}

/**
 * Allowlist for OTA HTTP redirect targets after the initial GitHub release URL.
 * GitHub serves assets via short-lived signed CDN URLs; re-check each hop (SEC-11).
 */
inline bool otaReleaseDownloadRedirectUrlAllowed(const char* url) {
    if (url == nullptr || url[0] == '\0') {
        return false;
    }
    if (otaReleaseDownloadUrlAllowed(url, OtaDownloadAsset::Firmware)
        || otaReleaseDownloadUrlAllowed(url, OtaDownloadAsset::Sha256)) {
        return true;
    }
    constexpr const char kHttps[] = "https://";
    if (strncmp(url, kHttps, sizeof(kHttps) - 1U) != 0) {
        return false;
    }
    const char* host = url + sizeof(kHttps) - 1U;
    if (host[0] == '\0' || host[0] == '/' || host[0] == '@') {
        return false;
    }
    // Reject userinfo (https://user:pass@host/...).
    const char* path = strchr(host, '/');
    const char* at   = strchr(host, '@');
    if (at != nullptr && (path == nullptr || at < path)) {
        return false;
    }
    constexpr const char* kCdnHosts[] = {
        "objects.githubusercontent.com",
        "release-assets.githubusercontent.com",
        "github-releases.githubusercontent.com",
    };
    for (const char* cdn : kCdnHosts) {
        const size_t n = strlen(cdn);
        if (strncmp(host, cdn, n) != 0) {
            continue;
        }
        const char next = host[n];
        // Exact host, optional port, or path.
        return next == '\0' || next == '/' || next == ':' || next == '?';
    }
    return false;
}
