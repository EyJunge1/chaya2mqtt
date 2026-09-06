#pragma once

inline auto queueCoalescePendingAfterPost(bool wasPending, bool queued) -> bool { return wasPending || !queued; }

inline auto queueCoalesceConsume(bool *pending) -> bool {
    if (pending == nullptr || !*pending) {
        return false;
    }
    *pending = false;
    return true;
}
