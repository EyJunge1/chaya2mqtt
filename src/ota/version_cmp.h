#pragma once

#include <cstdio>
#include <cstring>

/** Pure CalVer / beta (`-rc.N`) helpers for OTA (header-only, native-testable). */

struct OtaParsedVersion {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    unsigned rc = 0;
    bool isRc = false;
};

inline bool otaVersionParse(const char *tag, OtaParsedVersion *out) {
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
    int consumed = 0;
    if (sscanf(p, "%u.%u.%u%n", &out->major, &out->minor, &out->patch, &consumed) != 3) {
        return false;
    }
    if (out->major > 9999U || out->minor > 999U || out->patch > 999U) {
        return false;
    }
    const char *suffix = p + consumed;
    if (*suffix == '\0') {
        return true;
    }
    int rcConsumed = 0;
    if (sscanf(suffix, "-rc.%u%n", &out->rc, &rcConsumed) != 1 && sscanf(suffix, "-RC.%u%n", &out->rc, &rcConsumed) != 1) {
        return false;
    }
    if (out->rc == 0U || suffix[rcConsumed] != '\0') {
        return false;
    }
    out->isRc = true;
    return true;
}

inline bool otaVersionIsRc(const char *tag) {
    OtaParsedVersion parsed{};
    return otaVersionParse(tag, &parsed) && parsed.isRc;
}

/** Strict release-tag format shared with CI: vYYYY.M.PATCH[-rc.N]. */
inline bool otaReleaseTagIsAllowed(const char *tag) {
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
    if (strncmp(p, "-rc.", 4) != 0) {
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
inline bool otaVersionIsNewer(const char *remoteTag, const char *localVersion) {
    OtaParsedVersion remote{};
    if (!otaVersionParse(remoteTag, &remote)) {
        return false;
    }
    if (localVersion != nullptr && strcmp(localVersion, "dev") == 0) {
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
