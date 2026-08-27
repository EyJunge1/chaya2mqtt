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
