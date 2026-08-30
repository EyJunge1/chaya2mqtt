#pragma once

#include <ESPAsyncWebServer.h>

#include "async/sse_dirty.h"

void webEventsRegister(AsyncWebServer& ws);

void webEventsTick();

/** Mark SSE domains dirty (alias for producers that already include events). */
inline void webEventsMarkDirty(uint32_t bits) {
    sseMarkDirty(bits);
}
