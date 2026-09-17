#include "system_lifecycle.h"

std::atomic<bool> g_systemShutdownInProgress{false};
std::atomic<bool> g_chayaNvsWritesSuspended{false};
std::atomic<bool> g_factoryResetQueued{false};

bool systemShutdownTryClaim() {
    bool expected = false;
    return g_systemShutdownInProgress.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                              std::memory_order_acquire);
}

void systemShutdownRelease() { g_systemShutdownInProgress.store(false, std::memory_order_release); }
