#pragma once

#include <cstdint>

/**
 * SSE producer dirty bits (PERF-03). Producers call sseMarkDirty; webEventsTick
 * consumes via sseConsumeDirty. Lives in async/ so wifi/mqtt/heart never #include web/.
 */
constexpr uint32_t kSseChaya = 1U << 0U;
constexpr uint32_t kSseWifi = 1U << 1U;
constexpr uint32_t kSseMqtt = 1U << 2U;
constexpr uint32_t kSseOta = 1U << 3U;
constexpr uint32_t kSseDevice = 1U << 4U;
constexpr uint32_t kSseAll = 0x1Fu;

void sseMarkDirty(uint32_t bits);
/** Atomically take and clear pending bits. */
auto sseConsumeDirty() -> uint32_t;
