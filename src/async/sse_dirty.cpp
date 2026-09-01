#include "sse_dirty.h"

#include <atomic>

namespace {
std::atomic<uint32_t> s_pending{0};
} // namespace

void sseMarkDirty(uint32_t bits) {
    if (bits == 0U) {
        return;
    }
    s_pending.fetch_or(bits, std::memory_order_acq_rel);
}

uint32_t sseConsumeDirty() { return s_pending.exchange(0U, std::memory_order_acq_rel); }
