#pragma once

#include <cstdint>

/**
 * SSE producer dirty bits (PERF-03). Producers call sseMarkDirty; webEventsTick
 * consumes via sseConsumeDirty. Lives in async/ so wifi/mqtt/heart never #include web/.
 */
constexpr uint32_t kSseChaya = 1u << 0;
constexpr uint32_t kSseWifi = 1u << 1;
constexpr uint32_t kSseMqtt = 1u << 2;
constexpr uint32_t kSseOta = 1u << 3;
constexpr uint32_t kSseDevice = 1u << 4;
constexpr uint32_t kSseAll = 0x1Fu;

void sseMarkDirty(uint32_t bits);
/** Atomically take and clear pending bits. */
uint32_t sseConsumeDirty();
