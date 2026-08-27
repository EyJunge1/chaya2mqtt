#pragma once

inline bool queueCoalescePendingAfterPost(bool wasPending, bool queued) {
    return wasPending || !queued;
}

inline bool queueCoalesceConsume(bool* pending) {
    if (pending == nullptr || !*pending) {
        return false;
    }
    *pending = false;
    return true;
}
