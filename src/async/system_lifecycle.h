#pragma once

#include <atomic>

/** Cross-cutting lifecycle flag (not web-specific). Soft-off / shutdown in progress. */
extern std::atomic<bool> g_systemShutdownInProgress;
