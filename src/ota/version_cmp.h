#pragma once

#include <charconv>
#include <string_view>

/** Pure CalVer / beta (`-rc.N`) helpers for OTA (header-only, native-testable). */

struct OtaParsedVersion {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    unsigned rc = 0;
    bool isRc = false;
};

[[nodiscard]] constexpr auto otaParseUintToken(const char *p, unsigned maxVal, unsigned *out, const char **endOut) -> bool {
    if (p == nullptr || out == nullptr || endOut == nullptr || *p < '0' || *p > '9') {
        return false;
    }
    const char *digitsEnd = p;
    while (*digitsEnd >= '0' && *digitsEnd <= '9') {
        ++digitsEnd;
    }
    unsigned v = 0;
    const auto result = std::from_chars(p, digitsEnd, v);
    if (result.ec != std::errc{} || result.ptr == p || v > maxVal) {
        return false;
    }
    *out = v;
    *endOut = result.ptr;
    return true;
}

[[nodiscard]] constexpr auto otaVersionParse(const char *tag, OtaParsedVersion *out) -> bool {
    if (out == nullptr) {
        return false;
    }
    *out = OtaParsedVersion{};
    if (tag == nullptr || tag[0] == '\0') {
        return false;
    }
    const char *p = tag;
    if (p[0] == 'v' || p[0] == 'V') {
        ++p;
    }
    const char *end = nullptr;
    if (!otaParseUintToken(p, 9999U, &out->major, &end) || *end != '.') {
        return false;
    }
    p = end + 1;
    if (!otaParseUintToken(p, 999U, &out->minor, &end) || *end != '.') {
        return false;
    }
    p = end + 1;
    if (!otaParseUintToken(p, 999U, &out->patch, &end)) {
        return false;
    }
    if (*end == '\0') {
        return true;
    }
    const std::string_view rem{end};
    if (!rem.starts_with("-rc.") && !rem.starts_with("-RC.")) {
        return false;
    }
    p = end + 4;
    if (!otaParseUintToken(p, 9999U, &out->rc, &end) || *end != '\0' || out->rc == 0U) {
        return false;
    }
    out->isRc = true;
    return true;
}

[[nodiscard]] constexpr auto otaVersionIsRc(const char *tag) -> bool {
    OtaParsedVersion parsed{};
    return otaVersionParse(tag, &parsed) && parsed.isRc;
}

/** Strict release-tag format shared with CI: vYYYY.M.PATCH[-rc.N]. */
[[nodiscard]] constexpr auto otaReleaseTagIsAllowed(const char *tag) -> bool {
    if (tag == nullptr || tag[0] != 'v') {
        return false;
    }
    const char *p = tag + 1;
    for (unsigned i = 0; i < 4U; ++i) {
        if (p[i] < '0' || p[i] > '9') {
            return false;
        }
    }
    p += 4;
    if (*p++ != '.') {
        return false;
    }
    if (*p >= '1' && *p <= '9' && p[1] == '.') {
        p += 2;
    } else if (*p == '1' && p[1] >= '0' && p[1] <= '2' && p[2] == '.') {
        p += 3;
    } else {
        return false;
    }
    if (*p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        ++p;
    }
    if (*p == '\0') {
        return true;
    }
    if (!std::string_view{p}.starts_with("-rc.")) {
        return false;
    }
    p += 4;
    if (*p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        ++p;
    }
    OtaParsedVersion parsed{};
    return *p == '\0' && otaVersionParse(tag, &parsed) && parsed.isRc;
}

/** True if remote is a usable newer version than local (no downgrade). */
[[nodiscard]] constexpr auto otaVersionIsNewer(const char *remoteTag, const char *localVersion) -> bool {
    OtaParsedVersion remote{};
    if (!otaVersionParse(remoteTag, &remote)) {
        return false;
    }
    if (localVersion != nullptr && std::string_view{localVersion} == "dev") {
        return true;
    }
    OtaParsedVersion local{};
    if (!otaVersionParse(localVersion, &local)) {
        return false;
    }
    if (remote.major != local.major) {
        return remote.major > local.major;
    }
    if (remote.minor != local.minor) {
        return remote.minor > local.minor;
    }
    if (remote.patch != local.patch) {
        return remote.patch > local.patch;
    }
    if (remote.isRc != local.isRc) {
        return !remote.isRc; // Stable is newer than a beta (-rc.N) with the same base version.
    }
    return remote.isRc && remote.rc > local.rc;
}
