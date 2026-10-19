#pragma once

#include "web/wifi_status_cache_pure.h"

#include <ESPAsyncWebServer.h>

void webEventsRegister(AsyncWebServer &ws);

void webEventsTick();

/** Live STA snapshot, or last-good cache when the Wi-Fi API lock times out. */
auto webWifiStaNetSnapshotOrCached(WifiStaNetFields *out) -> bool;
