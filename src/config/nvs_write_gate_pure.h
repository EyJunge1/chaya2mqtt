#pragma once

#include <cstring>

#include "nvs_keys.h"

/**
 * Recheck after taking g_nvsMutex (BUG-LIFE-02 / BUG-LIFE-03).
 * Shutdown blocks cfg/wifi/mqtt but still allows a chaya flush.
 * Factory-suspend blocks chaya after the lock wait, even if the pre-lock check passed.
 */
inline auto nvsWriteAllowedAfterLock(bool shutdown, bool chayaSuspended, const char *ns) -> bool {
    if (shutdown && (ns == nullptr || strcmp(ns, kNvsNsChaya) != 0)) {
        return false;
    }
    if (chayaSuspended && ns != nullptr && strcmp(ns, kNvsNsChaya) == 0) {
        return false;
    }
    return true;
}
