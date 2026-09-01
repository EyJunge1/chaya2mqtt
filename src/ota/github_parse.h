#pragma once

#include "version_cmp.h"

#include <ArduinoJson.h>

#include <cstddef>
#include <cstring>

/** GitHub release JSON helpers (ArduinoJson, header-only, native-testable). */

inline bool otaDeserializeJson(const char *json, JsonDocument &doc) {
    if (json == nullptr) {
        return false;
    }
    return deserializeJson(doc, json) == DeserializationError::Ok;
}

inline bool otaCopyJsonString(JsonVariantConst v, char *out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!v.is<const char *>()) {
        return false;
    }
    const char *s = v.as<const char *>();
    if (s == nullptr || s[0] == '\0' || strlen(s) >= outLen) {
        return false;
    }
    strlcpy(out, s, outLen);
    return true;
}

inline bool otaParseJsonStringField(const char *json, const char *key, char *out, size_t outLen) {
    if (json == nullptr || key == nullptr || out == nullptr || outLen == 0U) {
        return false;
    }
    out[0] = '\0';
    JsonDocument doc;
    if (!otaDeserializeJson(json, doc)) {
        return false;
    }
    return otaCopyJsonString(doc[key], out, outLen);
}

inline bool otaParseJsonBoolField(JsonVariantConst obj, const char *key, bool *out) {
    if (key == nullptr || out == nullptr) {
        return false;
    }
    const JsonVariantConst v = obj[key];
    if (!v.is<bool>()) {
        return false;
    }
    *out = v.as<bool>();
    return true;
}

inline bool otaParseJsonBoolField(const char *json, const char *key, bool *out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    JsonDocument doc;
    if (!otaDeserializeJson(json, doc)) {
        return false;
    }
    return otaParseJsonBoolField(doc.as<JsonVariantConst>(), key, out);
}

inline bool otaJsonArrayHasAssetName(JsonArrayConst assets, const char *assetName) {
    if (assets.isNull() || assetName == nullptr) {
        return false;
    }
    for (JsonObjectConst asset : assets) {
        const char *name = asset["name"];
        if (name != nullptr && strcmp(name, assetName) == 0) {
            return true;
        }
    }
    return false;
}

inline bool otaJsonHasAssetName(JsonVariantConst root, const char *assetName) {
    if (assetName == nullptr || assetName[0] == '\0') {
        return false;
    }
    if (root.is<JsonArrayConst>()) {
        for (JsonObjectConst rel : root.as<JsonArrayConst>()) {
            if (otaJsonArrayHasAssetName(rel["assets"].as<JsonArrayConst>(), assetName)) {
                return true;
            }
        }
        return false;
    }
    if (root.is<JsonObjectConst>()) {
        return otaJsonArrayHasAssetName(root["assets"].as<JsonArrayConst>(), assetName);
    }
    return false;
}

inline bool otaJsonHasAssetName(const char *json, const char *assetName) {
    if (json == nullptr || assetName == nullptr || assetName[0] == '\0') {
        return false;
    }
    JsonDocument doc;
    if (!otaDeserializeJson(json, doc)) {
        return false;
    }
    return otaJsonHasAssetName(doc.as<JsonVariantConst>(), assetName);
}

inline bool otaReleaseHasRequiredAssets(JsonVariantConst root) {
    return otaJsonHasAssetName(root, "firmware.bin") && otaJsonHasAssetName(root, "firmware.sha256");
}

/**
 * Select release tag from a GitHub /releases JSON array.
 * preferPrerelease=true: newest non-draft prerelease, else newest non-draft stable.
 * preferPrerelease=false: newest non-draft stable.
 */
inline bool otaSelectReleaseFromListJson(JsonVariantConst root, bool preferPrerelease, char *tagOut, size_t tagLen,
                                         bool *outIsPrerelease) {
    if (tagOut == nullptr || tagLen == 0U) {
        return false;
    }
    tagOut[0] = '\0';
    if (outIsPrerelease != nullptr) {
        *outIsPrerelease = false;
    }
    if (!root.is<JsonArrayConst>()) {
        return false;
    }

    char bestStable[64]{};
    char bestPrerelease[64]{};

    for (JsonObjectConst rel : root.as<JsonArrayConst>()) {
        if (!rel["tag_name"].is<const char *>() || !rel["draft"].is<bool>() || !rel["prerelease"].is<bool>()) {
            continue;
        }
        const char *tag = rel["tag_name"].as<const char *>();
        const bool draft = rel["draft"].as<bool>();
        const bool pre = rel["prerelease"].as<bool>();
        if (tag == nullptr || draft || !otaReleaseTagIsAllowed(tag)) {
            continue;
        }
        char *best = pre ? bestPrerelease : bestStable;
        if (best[0] == '\0' || otaVersionIsNewer(tag + 1, best + 1)) {
            strlcpy(best, tag, 64U);
        }
    }

    const bool usePrerelease = preferPrerelease && bestPrerelease[0] != '\0';
    const char *selected = usePrerelease ? bestPrerelease : bestStable;
    if (selected[0] == '\0') {
        return false;
    }
    strlcpy(tagOut, selected, tagLen);
    if (outIsPrerelease != nullptr) {
        *outIsPrerelease = usePrerelease;
    }
    return true;
}

inline bool otaSelectReleaseFromListJson(const char *json, bool preferPrerelease, char *tagOut, size_t tagLen,
                                         bool *outIsPrerelease) {
    if (json == nullptr || tagOut == nullptr || tagLen == 0U) {
        return false;
    }
    tagOut[0] = '\0';
    if (outIsPrerelease != nullptr) {
        *outIsPrerelease = false;
    }
    JsonDocument doc;
    if (!otaDeserializeJson(json, doc)) {
        return false;
    }
    return otaSelectReleaseFromListJson(doc.as<JsonVariantConst>(), preferPrerelease, tagOut, tagLen, outIsPrerelease);
}
