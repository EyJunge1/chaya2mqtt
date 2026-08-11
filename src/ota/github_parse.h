#pragma once

#include <cstdio>
#include <cstddef>
#include <cstring>

/** Minimal GitHub release JSON helpers (header-only, native-testable). */

inline const char* otaJsonSkipWs(const char* p) {
    while (p != nullptr && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        ++p;
    }
    return p;
}

inline bool otaParseJsonStringField(const char* json, const char* key, char* out, size_t outLen) {
    if (json == nullptr || key == nullptr || out == nullptr || outLen == 0U) {
        return false;
    }
    out[0] = '\0';
    char needle[48];
    const int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(needle)) {
        return false;
    }
    const char* keyPos = strstr(json, needle);
    if (keyPos == nullptr) {
        return false;
    }
    const char* colon = strchr(keyPos + static_cast<size_t>(n), ':');
    if (colon == nullptr) {
        return false;
    }
    const char* p = otaJsonSkipWs(colon + 1);
    if (p == nullptr || *p != '\"') {
        return false;
    }
    ++p;
    size_t i = 0;
    while (*p != '\0' && *p != '\"' && (i + 1U) < outLen) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0U;
}

inline bool otaParseJsonBoolField(const char* json, const char* key, bool* out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    char needle[48];
    const int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(needle)) {
        return false;
    }
    const char* keyPos = strstr(json, needle);
    if (keyPos == nullptr) {
        return false;
    }
    const char* colon = strchr(keyPos + static_cast<size_t>(n), ':');
    if (colon == nullptr) {
        return false;
    }
    const char* p = otaJsonSkipWs(colon + 1);
    if (p == nullptr) {
        return false;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

inline bool otaJsonHasAssetName(const char* json, const char* assetName) {
    if (json == nullptr || assetName == nullptr || assetName[0] == '\0') {
        return false;
    }
    // GitHub JSON may include spaces after ':'. Match on the quoted asset name after "name".
    const char* cursor = json;
    while ((cursor = strstr(cursor, "\"name\"")) != nullptr) {
        const char* colon = strchr(cursor + 6, ':');
        if (colon == nullptr) {
            break;
        }
        const char* p = otaJsonSkipWs(colon + 1);
        if (p != nullptr && *p == '\"') {
            ++p;
            const size_t nameLen = strlen(assetName);
            if (strncmp(p, assetName, nameLen) == 0 && p[nameLen] == '\"') {
                return true;
            }
        }
        cursor += 6;
    }
    return false;
}

/**
 * Select release tag from a GitHub /releases JSON array.
 * preferPrerelease=true: first non-draft prerelease, else first non-draft stable.
 * preferPrerelease=false: first non-draft stable.
 */
inline bool otaSelectReleaseFromListJson(const char* json, bool preferPrerelease, char* tagOut,
                                        size_t tagLen, bool* outIsPrerelease) {
    if (json == nullptr || tagOut == nullptr || tagLen == 0U) {
        return false;
    }
    tagOut[0] = '\0';
    if (outIsPrerelease != nullptr) {
        *outIsPrerelease = false;
    }

    const char* cursor = strchr(json, '[');
    if (cursor == nullptr) {
        return false;
    }
    ++cursor;

    char fallbackStable[64]{};
    bool haveFallback = false;

    // GitHub places tag_name, draft and prerelease in each release's metadata before the
    // potentially large body/assets sections. Scanning metadata blocks means a bounded response
    // prefix is sufficient even when the complete /releases array exceeds device RAM.
    while ((cursor = strstr(cursor, "\"tag_name\"")) != nullptr) {
        const char* nextTag = strstr(cursor + 10, "\"tag_name\"");
        char        metadata[768]{};
        const size_t available =
            nextTag != nullptr ? static_cast<size_t>(nextTag - cursor) : strlen(cursor);
        const size_t copyLen =
            available < (sizeof(metadata) - 1U) ? available : (sizeof(metadata) - 1U);
        memcpy(metadata, cursor, copyLen);
        metadata[copyLen] = '\0';

        char tag[64]{};
        bool draft = false;
        bool pre   = false;
        if (!otaParseJsonStringField(metadata, "tag_name", tag, sizeof(tag))
            || !otaParseJsonBoolField(metadata, "draft", &draft)
            || !otaParseJsonBoolField(metadata, "prerelease", &pre)) {
            cursor += 10;
            continue;
        }
        cursor += 10;
        if (draft) {
            continue;
        }
        if (preferPrerelease) {
            if (pre) {
                strlcpy(tagOut, tag, tagLen);
                if (outIsPrerelease != nullptr) {
                    *outIsPrerelease = true;
                }
                return true;
            }
            if (!haveFallback) {
                strlcpy(fallbackStable, tag, sizeof(fallbackStable));
                haveFallback = true;
            }
        } else if (!pre) {
            strlcpy(tagOut, tag, tagLen);
            if (outIsPrerelease != nullptr) {
                *outIsPrerelease = false;
            }
            return true;
        }
    }

    if (preferPrerelease && haveFallback) {
        strlcpy(tagOut, fallbackStable, tagLen);
        if (outIsPrerelease != nullptr) {
            *outIsPrerelease = false;
        }
        return true;
    }
    return false;
}
