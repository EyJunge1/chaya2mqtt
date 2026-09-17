#pragma once

/** Queuing a GitHub check must not wipe a copied pending release (install may still race in). */
inline auto otaQueueCheckClearsPendingRelease() -> bool { return false; }

/** runGithubCheck may replace the copied release only when no install is queued. */
inline auto otaGithubCheckMayClearPendingRelease(bool installQueued) -> bool { return !installQueued; }
