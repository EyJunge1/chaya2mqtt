#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"
#include "wlan_recovery.h"

#include "ota/ota.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

void wlanRecoveryServiceLoop() {
    static WlanRecoveryState s_recovery{};

    if (!s_wifiSetupComplete.load(std::memory_order_acquire)
        || !s_bootWifiSettled.load(std::memory_order_acquire)) {
        return;
    }

    const bool apMode     = g_apMode.load(std::memory_order_relaxed);
    const bool connected  = wlanStaConnectedOk();
    const bool hasCreds   = s_activeWlanConfig.ssid[0] != '\0';
    const bool otaBlock   = otaBlocksDestructiveAction();
    const unsigned long nowMs = millis();

    const WlanRecoveryAction action =
        wlanRecoveryDecide(apMode, connected, otaBlock, hasCreds, nowMs, nowMs, s_recovery);

    switch (action) {
    case WlanRecoveryAction::None:
        break;
    case WlanRecoveryAction::ForcedReassoc:
        wlanForceStaReassoc("recovery");
        break;
    case WlanRecoveryAction::Restart:
        wlanControlledRestart("recovery-prolonged-outage");
        break;
    }
}
