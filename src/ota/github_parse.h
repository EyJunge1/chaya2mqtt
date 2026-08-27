#pragma once

#include "version_cmp.h"

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

inline const char* otaJsonStringEnd(const char* openingQuote);

inline bool otaJsonStringEquals(const char* openingQuote, const char* closingQuote,
                                const char* expected);

inline const char* otaJsonFindFieldValue(const char* json, const char* key) {
    if (json == nullptr || key == nullptr || key[0] == '\0') {
        return nullptr;
    }
    const char* first = otaJsonSkipWs(json);
    unsigned objectDepth = 0;
    const unsigned targetDepth = first != nullptr && *first == '{' ? 1U : 0U;
    for (const char* p = json; *p != '\0'; ++p) {
        if (*p == '{') {
            ++objectDepth;
            continue;
        }
        if (*p == '}') {
            if (objectDepth > 0U) {
                --objectDepth;
            }
            continue;
        }
        if (*p != '\"') {
            continue;
        }
        const char* keyEnd = otaJsonStringEnd(p);
        if (keyEnd == nullptr) {
            return nullptr;
        }
        const char* afterKey = otaJsonSkipWs(keyEnd + 1);
        if (objectDepth == targetDepth && otaJsonStringEquals(p, keyEnd, key)
            && afterKey != nullptr && *afterKey == ':') {
            return otaJsonSkipWs(afterKey + 1);
        }
        p = keyEnd;
    }
    return nullptr;
}

inline bool otaParseJsonStringField(const char* json, const char* key, char* out, size_t outLen) {
    if (json == nullptr || key == nullptr || out == nullptr || outLen == 0U) {
        return false;
    }
    out[0] = '\0';
    const char* p = otaJsonFindFieldValue(json, key);
    if (p == nullptr || *p != '\"') {
        return false;
    }
    ++p;
    size_t i = 0;
    while (*p != '\0' && *p != '\"') {
        char decoded = *p++;
        if (decoded == '\\') {
            const char escaped = *p++;
            switch (escaped) {
            case '\"':
            case '\\':
            case '/':
                decoded = escaped;
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            default:
                out[0] = '\0';
                return false;
            }
        }
        if ((i + 1U) >= outLen) {
            out[0] = '\0';
            return false;
        }
        out[i++] = decoded;
    }
    out[i] = '\0';
    return *p == '\"' && i > 0U;
}

inline bool otaParseJsonBoolField(const char* json, const char* key, bool* out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    const char* p = otaJsonFindFieldValue(json, key);
    if (p == nullptr) {
        return false;
    }
    const auto isBoundary = [](char c) {
        return c == '\0' || c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t'
               || c == '\r' || c == '\n';
    };
    if (strncmp(p, "true", 4) == 0 && isBoundary(p[4])) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0 && isBoundary(p[5])) {
        *out = false;
        return true;
    }
    return false;
}

inline const char* otaJsonStringEnd(const char* openingQuote) {
    if (openingQuote == nullptr || *openingQuote != '\"') {
        return nullptr;
    }
    const char* p = openingQuote + 1;
    while (*p != '\0') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p == '\"') {
            return p;
        }
        ++p;
    }
    return nullptr;
}

inline bool otaJsonStringEquals(const char* openingQuote, const char* closingQuote,
                                const char* expected) {
    if (openingQuote == nullptr || closingQuote == nullptr || expected == nullptr
        || closingQuote <= openingQuote) {
        return false;
    }
    const size_t actualLen = static_cast<size_t>(closingQuote - openingQuote - 1);
    const size_t expectedLen = strlen(expected);
    return actualLen == expectedLen && strncmp(openingQuote + 1, expected, expectedLen) == 0;
}

inline const char* otaJsonFindNextKey(const char* json, const char* key) {
    if (json == nullptr || key == nullptr) {
        return nullptr;
    }
    for (const char* p = json; *p != '\0'; ++p) {
        if (*p != '\"') {
            continue;
        }
        const char* end = otaJsonStringEnd(p);
        if (end == nullptr) {
            return nullptr;
        }
        const char* after = otaJsonSkipWs(end + 1);
        if (otaJsonStringEquals(p, end, key) && after != nullptr && *after == ':') {
            return p;
        }
        p = end;
    }
    return nullptr;
}

inline const char* otaJsonArrayEnd(const char* openingBracket) {
    if (openingBracket == nullptr || *openingBracket != '[') {
        return nullptr;
    }
    unsigned depth = 0;
    for (const char* p = openingBracket; *p != '\0'; ++p) {
        if (*p == '\"') {
            p = otaJsonStringEnd(p);
            if (p == nullptr) {
                return nullptr;
            }
        } else if (*p == '[') {
            ++depth;
        } else if (*p == ']') {
            if (--depth == 0U) {
                return p;
            }
        }
    }
    return nullptr;
}

inline const char* otaJsonObjectEnd(const char* openingBrace) {
    if (openingBrace == nullptr || *openingBrace != '{') {
        return nullptr;
    }
    unsigned depth = 0;
    for (const char* p = openingBrace; *p != '\0'; ++p) {
        if (*p == '\"') {
            p = otaJsonStringEnd(p);
            if (p == nullptr) {
                return nullptr;
            }
        } else if (*p == '{') {
            ++depth;
        } else if (*p == '}' && --depth == 0U) {
            return p;
        }
    }
    return nullptr;
}

inline bool otaParseReleaseObject(const char* begin, const char* end, char* tagOut, size_t tagLen,
                                  bool* draftOut, bool* prereleaseOut) {
    if (begin == nullptr || end == nullptr || begin >= end || tagOut == nullptr || tagLen == 0U
        || draftOut == nullptr || prereleaseOut == nullptr) {
        return false;
    }
    tagOut[0] = '\0';
    bool haveDraft = false;
    bool havePrerelease = false;
    unsigned depth = 0;
    for (const char* p = begin; p < end; ++p) {
        if (*p == '{') {
            ++depth;
            continue;
        }
        if (*p == '}') {
            if (depth > 0U) {
                --depth;
            }
            continue;
        }
        if (*p != '\"') {
            continue;
        }
        const char* keyEnd = otaJsonStringEnd(p);
        if (keyEnd == nullptr || keyEnd >= end) {
            return false;
        }
        const char* afterKey = otaJsonSkipWs(keyEnd + 1);
        if (depth != 1U || afterKey == nullptr || afterKey >= end || *afterKey != ':') {
            p = keyEnd;
            continue;
        }
        const char* value = otaJsonSkipWs(afterKey + 1);
        if (value == nullptr || value >= end) {
            return false;
        }
        if (otaJsonStringEquals(p, keyEnd, "tag_name") && *value == '\"') {
            const char* valueEnd = otaJsonStringEnd(value);
            if (valueEnd == nullptr || valueEnd > end) {
                return false;
            }
            const size_t len = static_cast<size_t>(valueEnd - value - 1);
            if (len == 0U || len >= tagLen || memchr(value + 1, '\\', len) != nullptr) {
                return false;
            }
            memcpy(tagOut, value + 1, len);
            tagOut[len] = '\0';
        } else if (otaJsonStringEquals(p, keyEnd, "draft")) {
            if (strncmp(value, "true", 4) == 0) {
                *draftOut = true;
                haveDraft = true;
            } else if (strncmp(value, "false", 5) == 0) {
                *draftOut = false;
                haveDraft = true;
            }
        } else if (otaJsonStringEquals(p, keyEnd, "prerelease")) {
            if (strncmp(value, "true", 4) == 0) {
                *prereleaseOut = true;
                havePrerelease = true;
            } else if (strncmp(value, "false", 5) == 0) {
                *prereleaseOut = false;
                havePrerelease = true;
            }
        }
        p = keyEnd;
    }
    return tagOut[0] != '\0' && haveDraft && havePrerelease;
}

inline bool otaJsonHasAssetName(const char* json, const char* assetName) {
    if (json == nullptr || assetName == nullptr || assetName[0] == '\0') {
        return false;
    }

    const char* cursor = json;
    while (*cursor != '\0') {
        if (*cursor != '\"') {
            ++cursor;
            continue;
        }

        const char* keyEnd = otaJsonStringEnd(cursor);
        if (keyEnd == nullptr) {
            return false;
        }
        const char* afterKey = otaJsonSkipWs(keyEnd + 1);
        if (!otaJsonStringEquals(cursor, keyEnd, "assets") || afterKey == nullptr
            || *afterKey != ':') {
            cursor = keyEnd + 1;
            continue;
        }

        const char* arrayStart = otaJsonSkipWs(afterKey + 1);
        const char* arrayEnd = otaJsonArrayEnd(arrayStart);
        if (arrayEnd == nullptr) {
            return false;
        }

        unsigned objectDepth = 0;
        for (const char* p = arrayStart + 1; p < arrayEnd; ++p) {
            if (*p == '{') {
                ++objectDepth;
                continue;
            }
            if (*p == '}') {
                if (objectDepth > 0U) {
                    --objectDepth;
                }
                continue;
            }
            if (*p != '\"') {
                continue;
            }

            const char* nameKeyEnd = otaJsonStringEnd(p);
            if (nameKeyEnd == nullptr || nameKeyEnd > arrayEnd) {
                return false;
            }
            const char* afterNameKey = otaJsonSkipWs(nameKeyEnd + 1);
            if (objectDepth == 1U && otaJsonStringEquals(p, nameKeyEnd, "name")
                && afterNameKey != nullptr && afterNameKey < arrayEnd && *afterNameKey == ':') {
                const char* valueStart = otaJsonSkipWs(afterNameKey + 1);
                const char* valueEnd = otaJsonStringEnd(valueStart);
                if (valueEnd != nullptr && valueEnd <= arrayEnd
                    && otaJsonStringEquals(valueStart, valueEnd, assetName)) {
                    return true;
                }
            }
            p = nameKeyEnd;
        }
        cursor = arrayEnd + 1;
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

    char bestStable[64]{};
    char bestPrerelease[64]{};

    while (*cursor != '\0') {
        if (*cursor != '{') {
            ++cursor;
            continue;
        }
        const char* objectEnd = otaJsonObjectEnd(cursor);
        const char* parseEnd  = objectEnd != nullptr ? objectEnd + 1 : cursor + strlen(cursor);
        char tag[64]{};
        bool draft = false;
        bool pre   = false;
        if (!otaParseReleaseObject(cursor, parseEnd, tag, sizeof(tag), &draft, &pre)) {
            cursor = parseEnd;
            continue;
        }
        if (draft || !otaReleaseTagIsAllowed(tag)) {
            cursor = parseEnd;
            continue;
        }
        char* best = pre ? bestPrerelease : bestStable;
        if (best[0] == '\0' || otaVersionIsNewer(tag + 1, best + 1)) {
            strlcpy(best, tag, 64U);
        }
        cursor = parseEnd;
    }

    const bool usePrerelease = preferPrerelease && bestPrerelease[0] != '\0';
    const char* selected     = usePrerelease ? bestPrerelease : bestStable;
    if (selected[0] == '\0') {
        return false;
    }
    strlcpy(tagOut, selected, tagLen);
    if (outIsPrerelease != nullptr) {
        *outIsPrerelease = usePrerelease;
    }
    return true;
}
