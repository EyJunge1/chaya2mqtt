#pragma once

#include <Preferences.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "async/system_lifecycle.h"
#include "async/task_handles.h"
#include "nvs_keys.h"

namespace app_nvs {

// Thread-safe Preferences helpers (global g_nvsMutex).

namespace detail {
inline void lock() {
    if (g_nvsMutex)
        xSemaphoreTake(g_nvsMutex, portMAX_DELAY);
}
inline void unlock() {
    if (g_nvsMutex)
        xSemaphoreGive(g_nvsMutex);
}

/** Block cfg/wifi/mqtt writes during shutdown; chaya flush may still persist (RC-LIFE-02 / RC-MQTT-2). */
inline bool writesBlocked(const char *ns) {
    if (!g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        return false;
    }
    return ns == nullptr || strcmp(ns, kNvsNsChaya) != 0;
}
} // namespace detail

inline bool writesBlocked(const char *ns) { return detail::writesBlocked(ns); }

/** RAII lock for one Preferences session (pair with app_nvs::* calls if mixing raw Preferences). */
class ScopedNvsLock {
  public:
    ScopedNvsLock() { detail::lock(); }
    ~ScopedNvsLock() { detail::unlock(); }
    ScopedNvsLock(const ScopedNvsLock &) = delete;
    ScopedNvsLock &operator=(const ScopedNvsLock &) = delete;
};

inline bool clearNamespace(const char *ns) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        detail::unlock();
        return false;
    }
    const bool ok = prefs.clear();
    prefs.end();
    detail::unlock();
    return ok;
}

inline bool hasKey(const char *ns, const char *key) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        detail::unlock();
        return false;
    }
    const bool ok = prefs.isKey(key);
    prefs.end();
    detail::unlock();
    return ok;
}

inline uint8_t readUChar(const char *ns, const char *key, uint8_t defaultVal) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        detail::unlock();
        return defaultVal;
    }
    const uint8_t v = prefs.getUChar(key, defaultVal);
    prefs.end();
    detail::unlock();
    return v;
}

inline bool writeUChar(const char *ns, const char *key, uint8_t value) {
    if (detail::writesBlocked(ns)) {
        return false;
    }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        detail::unlock();
        return false;
    }
    const size_t w = prefs.putUChar(key, value);
    prefs.end();
    detail::unlock();
    return w > 0U;
}

inline uint32_t readUInt(const char *ns, const char *key, uint32_t defaultVal) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        detail::unlock();
        return defaultVal;
    }
    const uint32_t v = prefs.getUInt(key, defaultVal);
    prefs.end();
    detail::unlock();
    return v;
}

inline bool writeUInt(const char *ns, const char *key, uint32_t value) {
    if (detail::writesBlocked(ns)) {
        return false;
    }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        detail::unlock();
        return false;
    }
    const size_t w = prefs.putUInt(key, value);
    prefs.end();
    detail::unlock();
    return w > 0U;
}

inline int readInt(const char *ns, const char *key, int defaultVal) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        detail::unlock();
        return defaultVal;
    }
    const int v = prefs.getInt(key, defaultVal);
    prefs.end();
    detail::unlock();
    return v;
}

inline bool writeInt(const char *ns, const char *key, int value) {
    if (detail::writesBlocked(ns)) {
        return false;
    }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        detail::unlock();
        return false;
    }
    const size_t w = prefs.putInt(key, value);
    prefs.end();
    detail::unlock();
    return w > 0U;
}

inline size_t readString(const char *ns, const char *key, char *out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return 0;
    }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        out[0] = '\0';
        detail::unlock();
        return 0;
    }
    const size_t n = prefs.getString(key, out, outLen);
    prefs.end();
    detail::unlock();
    return n;
}

/** putString returns strlen; 0 is expected for "" (arduino-esp32 #12869). Verify empty via isKey. */
inline bool putStringOk(Preferences &prefs, const char *key, const char *value) {
    const char *v = value != nullptr ? value : "";
    const size_t w = prefs.putString(key, v);
    if (w > 0U) {
        return true;
    }
    return v[0] == '\0' && prefs.isKey(key);
}

inline bool writeString(const char *ns, const char *key, const char *value) {
    if (detail::writesBlocked(ns)) {
        return false;
    }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        detail::unlock();
        return false;
    }
    const bool ok = putStringOk(prefs, key, value);
    prefs.end();
    detail::unlock();
    return ok;
}

inline bool writeBytes(const char *ns, const char *key, const void *data, size_t len) {
    if (detail::writesBlocked(ns) || data == nullptr || len == 0U) {
        return false;
    }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        detail::unlock();
        return false;
    }
    const size_t w = prefs.putBytes(key, data, len);
    prefs.end();
    detail::unlock();
    return w == len;
}

inline bool readBytes(const char *ns, const char *key, void *out, size_t len) {
    if (out == nullptr || len == 0U) {
        return false;
    }
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        detail::unlock();
        return false;
    }
    const bool ok = prefs.getBytesLength(key) == len && prefs.getBytes(key, out, len) == len;
    prefs.end();
    detail::unlock();
    return ok;
}

} // namespace app_nvs
