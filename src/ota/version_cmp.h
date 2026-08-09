#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

/** Pure CalVer / RC helpers for OTA (header-only, native-testable). */

inline bool otaVersionIsRc(const char* tag) {
    if (tag == nullptr || tag[0] == '\0') {
        return false;
    }
    const char* p = tag;
    if (p[0] == 'v' || p[0] == 'V') {
        ++p;
    }
    return (strstr(p, "-rc.") != nullptr) || (strstr(p, "-RC.") != nullptr);
}

inline unsigned otaVersionRcNumber(const char* tag) {
    if (tag == nullptr) {
        return 0;
    }
    const char* p = strstr(tag, "-rc.");
    if (p == nullptr) {
        p = strstr(tag, "-RC.");
    }
    if (p == nullptr) {
        return 0;
    }
    unsigned rc = 0;
    if (sscanf(p + 4, "%u", &rc) != 1) {
        return 0;
    }
    return rc;
}

/**
 * Pack YYYY.M.PATCH (+ RC ordinal).
 * Layout: base*1000 + rcComponent where rcComponent is 999 for stable, else RC number (1..998).
 * Returns 0 on empty/invalid, UINT32_MAX if out of range.
 */
inline uint32_t otaVersionPack(const char* tag) {
    if (tag == nullptr || tag[0] == '\0') {
        return 0;
    }
    const char* p = tag;
    if (p[0] == 'v' || p[0] == 'V') {
        ++p;
    }
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    const int n = sscanf(p, "%u.%u.%u", &major, &minor, &patch);
    if (n < 1) {
        return 0;
    }
    if (major > 9999U || minor > 999U || patch > 999U) {
        return UINT32_MAX;
    }
    const uint32_t base = major * 100000U + minor * 1000U + patch;
    unsigned rcComponent = 999U;
    if (otaVersionIsRc(p)) {
        const unsigned rc = otaVersionRcNumber(p);
        if (rc == 0U || rc > 998U) {
            return UINT32_MAX;
        }
        rcComponent = rc;
    }
    // Stable (999) sorts above any RC with the same YYYY.M.PATCH.
    return base * 1000U + rcComponent;
}

/** True if remote is a usable newer version than local (no downgrade). */
inline bool otaVersionIsNewer(const char* remoteTag, const char* localVersion) {
    const uint32_t remoteV = otaVersionPack(remoteTag);
    const uint32_t localV  = otaVersionPack(localVersion);
    if (remoteV == 0U || localV == 0U || remoteV == UINT32_MAX || localV == UINT32_MAX) {
        return false;
    }
    return remoteV > localV;
}
