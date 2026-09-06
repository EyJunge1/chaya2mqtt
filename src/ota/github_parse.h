#pragma once

#include "version_cmp.h"

#include <ArduinoJson.h>

#include <cstddef>
#include <cstring>
#include <utility>

/** GitHub release JSON helpers (ArduinoJson, header-only, native-testable). */

inline auto otaDeserializeJson(const char *json, JsonDocument &doc) -> bool {
    if (json == nullptr) {
        return false;
    }
    return deserializeJson(doc, json) == DeserializationError::Ok && !doc.overflowed();
}

inline auto otaGithubJsonRootIsArray(const char *json) -> bool {
    if (json == nullptr) {
        return false;
    }
    for (const char *p = json; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            continue;
        }
        return *p == '[';
    }
    return false;
}

/** Keep only tag_name / draft / prerelease / assets[].name (object or list root). */
inline void otaFillGithubReleaseFilter(JsonDocument &filter, bool list) {
    filter.clear();
    JsonObject rel = list ? filter[0].to<JsonObject>() : filter.to<JsonObject>();
    rel["tag_name"] = true;
    rel["draft"] = true;
    rel["prerelease"] = true;
    rel["assets"][0]["name"] = true;
}

template <typename TInput> inline auto otaDeserializeGithubReleaseJson(TInput &&input, JsonDocument &doc, bool list) -> bool {
    JsonDocument filter;
    otaFillGithubReleaseFilter(filter, list);
    return deserializeJson(doc, std::forward<TInput>(input), DeserializationOption::Filter(filter)) == DeserializationError::Ok &&
           !doc.overflowed();
}

inline auto otaDeserializeGithubReleaseJson(const char *json, JsonDocument &doc) -> bool {
    if (json == nullptr) {
        return false;
    }
    return otaDeserializeGithubReleaseJson(json, doc, otaGithubJsonRootIsArray(json));
}

inline auto otaCopyJsonString(JsonVariantConst v, char *out, size_t outLen) -> bool {
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

inline auto otaParseJsonStringField(const char *json, const char *key, char *out, size_t outLen) -> bool {
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

inline auto otaParseJsonBoolField(JsonVariantConst obj, const char *key, bool *out) -> bool {
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

inline auto otaParseJsonBoolField(const char *json, const char *key, bool *out) -> bool {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    JsonDocument doc;
    if (!otaDeserializeJson(json, doc)) {
        return false;
    }
    return otaParseJsonBoolField(doc.as<JsonVariantConst>(), key, out);
}

inline auto otaJsonArrayHasAssetName(JsonArrayConst assets, const char *assetName) -> bool {
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

inline auto otaJsonHasAssetName(JsonVariantConst root, const char *assetName) -> bool {
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

inline auto otaJsonHasAssetName(const char *json, const char *assetName) -> bool {
    if (json == nullptr || assetName == nullptr || assetName[0] == '\0') {
        return false;
    }
    JsonDocument doc;
    if (!otaDeserializeGithubReleaseJson(json, doc)) {
        return false;
    }
    return otaJsonHasAssetName(doc.as<JsonVariantConst>(), assetName);
}

inline auto otaReleaseHasRequiredAssets(JsonVariantConst root) -> bool {
    if (!root.is<JsonObjectConst>()) {
        return false;
    }
    bool hasBin = false;
    bool hasSha = false;
    for (JsonObjectConst asset : root["assets"].as<JsonArrayConst>()) {
        const char *name = asset["name"];
        if (name == nullptr) {
            continue;
        }
        if (!hasBin && strcmp(name, "firmware.bin") == 0) {
            hasBin = true;
        } else if (!hasSha && strcmp(name, "firmware.sha256") == 0) {
            hasSha = true;
        }
        if (hasBin && hasSha) {
            return true;
        }
    }
    return false;
}

/**
 * Select release tag from a GitHub /releases JSON array.
 * preferPrerelease=true: newest non-draft prerelease, else newest non-draft stable.
 * preferPrerelease=false: newest non-draft stable.
 * requireAssets: skip releases that lack firmware.bin + firmware.sha256.
 */
inline auto otaSelectReleaseFromListJson(JsonVariantConst root, bool preferPrerelease, char *tagOut, size_t tagLen,
                                         bool *outIsPrerelease, bool requireAssets = false) -> bool {
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
        if (requireAssets && !otaReleaseHasRequiredAssets(rel)) {
            continue;
        }
        char *best = pre ? bestPrerelease : bestStable;
        if (best[0] == '\0' || otaVersionIsNewer(tag, best)) {
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

inline auto otaSelectReleaseFromListJson(const char *json, bool preferPrerelease, char *tagOut, size_t tagLen,
                                         bool *outIsPrerelease, bool requireAssets = false) -> bool {
    if (json == nullptr || tagOut == nullptr || tagLen == 0U) {
        return false;
    }
    tagOut[0] = '\0';
    if (outIsPrerelease != nullptr) {
        *outIsPrerelease = false;
    }
    JsonDocument doc;
    if (!otaDeserializeGithubReleaseJson(json, doc, true)) {
        return false;
    }
    return otaSelectReleaseFromListJson(doc.as<JsonVariantConst>(), preferPrerelease, tagOut, tagLen, outIsPrerelease,
                                        requireAssets);
}
