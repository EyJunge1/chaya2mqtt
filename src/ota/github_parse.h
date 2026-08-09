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
 * Extract one top-level JSON object from an array, string-aware.
 * On success, *objBegin points at '{', *objEnd past matching '}'.
 */
inline bool otaJsonNextArrayObject(const char*& cursor, const char*& objBegin, const char*& objEnd) {
    if (cursor == nullptr) {
        return false;
    }
    const char* p = cursor;
    while (*p != '\0' && *p != '{') {
        ++p;
    }
    if (*p != '{') {
        return false;
    }
    objBegin          = p;
    int  depth        = 0;
    bool inString     = false;
    bool escape       = false;
    const char* q     = p;
    for (; *q != '\0'; ++q) {
        const char c = *q;
        if (inString) {
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '\"') {
                inString = false;
            }
            continue;
        }
        if (c == '\"') {
            inString = true;
            continue;
        }
        if (c == '{') {
            ++depth;
            continue;
        }
        if (c == '}') {
            --depth;
            if (depth == 0) {
                objEnd = q + 1;
                cursor = objEnd;
                return true;
            }
        }
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

    char        fallbackStable[64]{};
    bool        haveFallback = false;
    const char* objBegin     = nullptr;
    const char* objEnd       = nullptr;

    while (otaJsonNextArrayObject(cursor, objBegin, objEnd)) {
        const size_t objLen = static_cast<size_t>(objEnd - objBegin);
        // Copy into a bounded scratch so field parsers see a NUL-terminated object.
        char scratch[2048];
        if (objLen + 1U > sizeof(scratch)) {
            // Object too large (long body); still try to parse from prefix which has tags first.
            const size_t copyLen = sizeof(scratch) - 1U;
            memcpy(scratch, objBegin, copyLen);
            scratch[copyLen] = '\0';
        } else {
            memcpy(scratch, objBegin, objLen);
            scratch[objLen] = '\0';
        }

        bool draft = false;
        bool pre   = false;
        (void)otaParseJsonBoolField(scratch, "draft", &draft);
        (void)otaParseJsonBoolField(scratch, "prerelease", &pre);
        if (draft) {
            continue;
        }

        char tag[64]{};
        if (!otaParseJsonStringField(scratch, "tag_name", tag, sizeof(tag))) {
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
