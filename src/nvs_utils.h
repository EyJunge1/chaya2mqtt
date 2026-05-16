#pragma once

#include <Preferences.h>
#include <cstddef>
#include <cstdint>

/** Thin wrappers around Arduino Preferences (NVS) for fewer open/close mistakes. */
namespace app_nvs {

inline uint8_t readUChar(const char* ns, const char* key, uint8_t defaultVal) {
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return defaultVal;
    }
    const uint8_t v = prefs.getUChar(key, defaultVal);
    prefs.end();
    return v;
}

inline bool writeUChar(const char* ns, const char* key, uint8_t value) {
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    prefs.putUChar(key, value);
    prefs.end();
    return true;
}

inline uint32_t readUInt(const char* ns, const char* key, uint32_t defaultVal) {
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return defaultVal;
    }
    const uint32_t v = prefs.getUInt(key, defaultVal);
    prefs.end();
    return v;
}

inline bool writeUInt(const char* ns, const char* key, uint32_t value) {
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    prefs.putUInt(key, value);
    prefs.end();
    return true;
}

inline int readInt(const char* ns, const char* key, int defaultVal) {
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return defaultVal;
    }
    const int v = prefs.getInt(key, defaultVal);
    prefs.end();
    return v;
}

inline bool writeInt(const char* ns, const char* key, int value) {
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    prefs.putInt(key, value);
    prefs.end();
    return true;
}

/** Reads stored string into out (NUL-terminated); returns Arduino getString length (may be 0). */
inline size_t readString(const char* ns, const char* key, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return 0;
    }
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        out[0] = '\0';
        return 0;
    }
    const size_t n = prefs.getString(key, out, outLen);
    prefs.end();
    return n;
}

inline bool writeString(const char* ns, const char* key, const char* value) {
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    prefs.putString(key, value != nullptr ? value : "");
    prefs.end();
    return true;
}

} // namespace app_nvs
