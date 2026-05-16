#pragma once

#include <cstddef>
#include <cstdint>

/** STA credential test during AP setup (before NVS commit); runs on main task via wifiConnectionTestServiceLoop(). */
enum class WlanWifiConnectionTestState : uint8_t {
    Idle    = 0,
    Testing = 1,
    Ok      = 2,
    Fail    = 3,
};

void wifiConnectionTestServiceLoop();

/** Start STA join test while softAP stays up (AP mode only). */
bool wlanStartWifiConnectionTest(const char* ssid, const char* password);

/** Stop test, disconnect STA interface, reset to Idle. */
void wlanAbortWifiConnectionTest();

WlanWifiConnectionTestState wlanGetWifiConnectionTestState();

/** SSID currently being tested or last result context; false if Idle. */
bool wlanWifiConnectionTestSsidSnapshot(char* outSsid, size_t maxLen);

/** If state Ok and STA still has IPv4: write NVS, schedule reboot. */
bool wlanCommitWifiConnectionTestAndScheduleReboot();
