#pragma once

#include <cstddef>
#include <cstdint>

#include "wlan_config.h"

/** STA credential test during AP setup (before NVS commit); runs on main task via wifiConnectionTestServiceLoop(). */
enum class WlanWifiConnectionTestState : uint8_t {
    Idle = 0,
    Testing = 1,
    Ok = 2,
    Fail = 3,
};

void wifiConnectionTestServiceLoop();

/** Start STA join test while softAP stays up (AP mode only). */
auto wlanStartWifiConnectionTest(const WlanConfig &cfg) -> bool;

/** Re-run STA join with RAM credentials from a Fail state (AP mode only). */
auto wlanRetryWifiConnectionTest() -> bool;

/** Stop test, disconnect STA interface, reset to Idle. */
void wlanAbortWifiConnectionTest();

auto wlanGetWifiConnectionTestState() -> WlanWifiConnectionTestState;

/** SSID currently being tested or last result context; false if Idle. */
auto wlanWifiConnectionTestSsidSnapshot(char *outSsid, size_t maxLen) -> bool;

/** If state Ok and STA still has IPv4: write NVS, schedule reboot. */
auto wlanCommitWifiConnectionTestAndScheduleReboot() -> bool;
