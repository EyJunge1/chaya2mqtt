#pragma once

#include <atomic>

/** Cross-cutting lifecycle flag (not web-specific). Soft-off / shutdown in progress. */
extern std::atomic<bool> g_systemShutdownInProgress;

/** Factory-reset wipe in progress — chaya NVS writes must fail even after waiting for g_nvsMutex. */
extern std::atomic<bool> g_chayaNvsWritesSuspended;

/** Set in HTTP POST before NetCmd so Soft-off / admin restart cannot race the wipe. */
extern std::atomic<bool> g_factoryResetQueued;

/** CAS false→true. Caller who wins must Release on abort; never Release a foreign claim. */
auto systemShutdownTryClaim() -> bool;

/** Clear shutdown only after a successful TryClaim by the same owner. */
void systemShutdownRelease();
