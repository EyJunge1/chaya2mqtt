#pragma once

/** runGithubCheck may replace the copied release only when no install is queued. */
inline auto otaGithubCheckMayClearPendingRelease(bool installQueued) -> bool { return !installQueued; }
