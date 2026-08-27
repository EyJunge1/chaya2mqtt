#pragma once

#include <Preferences.h>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "async/task_handles.h"

namespace app_nvs {

// Thread-safe Preferences helpers (global g_nvsMutex).

namespace detail {
inline void lock()   { if (g_nvsMutex) xSemaphoreTake(g_nvsMutex, portMAX_DELAY); }
inline void unlock() { if (g_nvsMutex) xSemaphoreGive(g_nvsMutex); }
} // namespace detail

/** RAII lock for one Preferences session (pair with app_nvs::* calls if mixing raw Preferences). */
class ScopedNvsLock {
public:
    ScopedNvsLock()  { detail::lock(); }
    ~ScopedNvsLock() { detail::unlock(); }
    ScopedNvsLock(const ScopedNvsLock&)            = delete;
    ScopedNvsLock& operator=(const ScopedNvsLock&) = delete;
};

inline bool clearNamespace(const char* ns) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) { detail::unlock(); return false; }
    const bool ok = prefs.clear();
    prefs.end();
    detail::unlock();
    return ok;
}

inline bool hasKey(const char* ns, const char* key) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) { detail::unlock(); return false; }
    const bool ok = prefs.isKey(key);
    prefs.end();
    detail::unlock();
    return ok;
}

inline uint8_t readUChar(const char* ns, const char* key, uint8_t defaultVal) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) { detail::unlock(); return defaultVal; }
    const uint8_t v = prefs.getUChar(key, defaultVal);
    prefs.end();
    detail::unlock();
    return v;
}

inline bool writeUChar(const char* ns, const char* key, uint8_t value) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) { detail::unlock(); return false; }
    const size_t w = prefs.putUChar(key, value);
    prefs.end();
    detail::unlock();
    return w > 0U;
}

inline uint32_t readUInt(const char* ns, const char* key, uint32_t defaultVal) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) { detail::unlock(); return defaultVal; }
    const uint32_t v = prefs.getUInt(key, defaultVal);
    prefs.end();
    detail::unlock();
    return v;
}

inline bool writeUInt(const char* ns, const char* key, uint32_t value) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) { detail::unlock(); return false; }
    const size_t w = prefs.putUInt(key, value);
    prefs.end();
    detail::unlock();
    return w > 0U;
}

inline int readInt(const char* ns, const char* key, int defaultVal) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) { detail::unlock(); return defaultVal; }
    const int v = prefs.getInt(key, defaultVal);
    prefs.end();
    detail::unlock();
    return v;
}

inline bool writeInt(const char* ns, const char* key, int value) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) { detail::unlock(); return false; }
    const size_t w = prefs.putInt(key, value);
    prefs.end();
    detail::unlock();
    return w > 0U;
}

inline size_t readString(const char* ns, const char* key, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U) { return 0; }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) { out[0] = '\0'; detail::unlock(); return 0; }
    const size_t n = prefs.getString(key, out, outLen);
    prefs.end();
    detail::unlock();
    return n;
}

inline bool writeString(const char* ns, const char* key, const char* value) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) { detail::unlock(); return false; }
    const size_t w = prefs.putString(key, value != nullptr ? value : "");
    prefs.end();
    detail::unlock();
    return w > 0U;
}

} // namespace app_nvs
