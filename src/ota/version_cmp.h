#pragma once

#include <cstdio>
#include <cstring>

/** Pure CalVer / RC helpers for OTA (header-only, native-testable). */

struct OtaParsedVersion {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    unsigned rc    = 0;
    bool     isRc  = false;
};

inline bool otaVersionParse(const char* tag, OtaParsedVersion* out) {
    if (out == nullptr) {
        return false;
    }
    *out = OtaParsedVersion{};
    if (tag == nullptr || tag[0] == '\0') {
        return false;
    }
    const char* p = tag;
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
    const char* suffix = p + consumed;
    if (*suffix == '\0') {
        return true;
    }
    int rcConsumed = 0;
    if (sscanf(suffix, "-rc.%u%n", &out->rc, &rcConsumed) != 1
        && sscanf(suffix, "-RC.%u%n", &out->rc, &rcConsumed) != 1) {
        return false;
    }
    if (out->rc == 0U || suffix[rcConsumed] != '\0') {
        return false;
    }
    out->isRc = true;
    return true;
}

inline bool otaVersionIsRc(const char* tag) {
    OtaParsedVersion parsed{};
    return otaVersionParse(tag, &parsed) && parsed.isRc;
}

/** True if remote is a usable newer version than local (no downgrade). */
inline bool otaVersionIsNewer(const char* remoteTag, const char* localVersion) {
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
        return !remote.isRc; // Stable is newer than an RC with the same base version.
    }
    return remote.isRc && remote.rc > local.rc;
}
