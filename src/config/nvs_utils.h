#pragma once

#include <Preferences.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "async/system_lifecycle.h"
#include "async/task_handles.h"
#include "nvs_keys.h"
#include "nvs_write_gate_pure.h"

namespace app_nvs {

// Thread-safe Preferences helpers (global g_nvsMutex).

/** Default write-lock wait. Settings apply may temporarily tighten this (RC-WEB-42). */
inline std::atomic<TickType_t> g_nvsWriteLockTimeoutTicks{portMAX_DELAY};

constexpr TickType_t kNvsSettingsApplyLockTimeoutTicks = pdMS_TO_TICKS(500);

class ScopedNvsLockTimeout {
  public:
    explicit ScopedNvsLockTimeout(TickType_t ticks)
        : prev_(g_nvsWriteLockTimeoutTicks.exchange(ticks, std::memory_order_acq_rel)) {}
    ~ScopedNvsLockTimeout() { g_nvsWriteLockTimeoutTicks.store(prev_, std::memory_order_release); }
    ScopedNvsLockTimeout(const ScopedNvsLockTimeout &) = delete;
    ScopedNvsLockTimeout &operator=(const ScopedNvsLockTimeout &) = delete;

  private:
    TickType_t prev_;
};

namespace detail {
inline void lock() {
    if (g_nvsMutex)
        xSemaphoreTake(g_nvsMutex, portMAX_DELAY);
}
inline bool lockTimed(TickType_t ticks) {
    if (!g_nvsMutex) {
        return true;
    }
    return xSemaphoreTake(g_nvsMutex, ticks) == pdTRUE;
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

/** Fast-path of the post-lock gate (BUG-LIFE-02 / BUG-LIFE-03). */
inline bool writePermitted(const char *ns) {
    return nvsWriteAllowedAfterLock(g_systemShutdownInProgress.load(std::memory_order_acquire),
                                    g_chayaNvsWritesSuspended.load(std::memory_order_acquire), ns);
}

/** BUG-LIFE-02 / BUG-LIFE-03: fail closed before waiting if shutdown already blocks the ns. */
inline bool lockUnlessWritesBlocked(const char *ns) {
    if (!writePermitted(ns)) {
        return false;
    }
    const TickType_t timeout = g_nvsWriteLockTimeoutTicks.load(std::memory_order_acquire);
    if (!lockTimed(timeout)) {
        return false;
    }
    if (!writePermitted(ns)) {
        unlock();
        return false;
    }
    return true;
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

/** RAII write lock: fails closed if shutdown blocked the namespace after waiting for g_nvsMutex. */
class ScopedNvsWriteLock {
  public:
    explicit ScopedNvsWriteLock(const char *ns) : held_(detail::lockUnlessWritesBlocked(ns)) {}
    ~ScopedNvsWriteLock() {
        if (held_) {
            detail::unlock();
        }
    }
    explicit operator bool() const { return held_; }
    ScopedNvsWriteLock(const ScopedNvsWriteLock &) = delete;
    ScopedNvsWriteLock &operator=(const ScopedNvsWriteLock &) = delete;

  private:
    bool held_;
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

struct NvsClearProgress {
    bool allCleared = false;
    bool anyMutated = false;
};

/** Caller must hold g_nvsMutex (RC-LIFE-04: pair with nvs_flash_erase under the same lock).
 *  Stops at the first failure so a partial wipe is a prefix, not a holey set (BUG-LIFE-05). */
inline NvsClearProgress clearNamespacesUnlocked(const char *const *nsList, size_t count) {
    NvsClearProgress result{};
    if (nsList == nullptr || count == 0U) {
        return result;
    }
    result.allCleared = true;
    for (size_t i = 0; i < count; ++i) {
        Preferences prefs;
        if (nsList[i] == nullptr || !prefs.begin(nsList[i], false)) {
            result.allCleared = false;
            break;
        }
        const bool cleared = prefs.clear();
        prefs.end();
        if (!cleared) {
            result.allCleared = false;
            break;
        }
        result.anyMutated = true;
    }
    return result;
}

/** BUG-LIFE-02: wipe several namespaces under one g_nvsMutex hold. */
inline bool clearNamespaces(const char *const *nsList, size_t count) {
    if (nsList == nullptr || count == 0U) {
        return false;
    }
    detail::lock();
    const bool ok = clearNamespacesUnlocked(nsList, count).allCleared;
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

inline bool removeKey(const char *ns, const char *key) {
    if (!detail::writePermitted(ns)) {
        return false;
    }
    if (!detail::lockUnlessWritesBlocked(ns)) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        detail::unlock();
        return false;
    }
    const bool ok = prefs.remove(key);
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
    if (!detail::writePermitted(ns)) {
        return false;
    }
    if (!detail::lockUnlessWritesBlocked(ns)) {
        return false;
    }
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
    if (!detail::writePermitted(ns)) {
        return false;
    }
    if (!detail::lockUnlessWritesBlocked(ns)) {
        return false;
    }
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
    if (!detail::writePermitted(ns)) {
        return false;
    }
    if (!detail::lockUnlessWritesBlocked(ns)) {
        return false;
    }
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
    if (!detail::writePermitted(ns)) {
        return false;
    }
    if (!detail::lockUnlessWritesBlocked(ns)) {
        return false;
    }
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
    if (!detail::writePermitted(ns) || data == nullptr || len == 0U) {
        return false;
    }
    if (!detail::lockUnlessWritesBlocked(ns)) {
        return false;
    }
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

inline size_t bytesLength(const char *ns, const char *key) {
    detail::lock();
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        detail::unlock();
        return 0;
    }
    const size_t n = prefs.getBytesLength(key);
    prefs.end();
    detail::unlock();
    return n;
}

} // namespace app_nvs
