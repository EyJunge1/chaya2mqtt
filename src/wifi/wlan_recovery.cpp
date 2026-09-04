#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"
#include "wlan_recovery.h"

#include "async/web_server_hooks.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "ota/ota.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <time.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

namespace {

constexpr const char kNvsKeyWifiRecDay[] = "rec_day";
constexpr const char kNvsKeyWifiRecRest[] = "rec_rst";

uint8_t recoveryRestartsUsedToday() {
    time_t nowSec = time(nullptr);
    const uint32_t day = (nowSec > 1700000000) ? static_cast<uint32_t>(nowSec / 86400) : 0U;
    const uint32_t storedDay = app_nvs::readUInt(kNvsNsWifi, kNvsKeyWifiRecDay, 0U);
    if (day == 0U || storedDay != day) {
        return 0U;
    }
    const uint8_t n = app_nvs::readUChar(kNvsNsWifi, kNvsKeyWifiRecRest, 0U);
    return n;
}

void recoveryNoteRestart() {
    time_t nowSec = time(nullptr);
    const uint32_t day = (nowSec > 1700000000) ? static_cast<uint32_t>(nowSec / 86400) : 0U;
    if (day == 0U) {
        return;
    }
    const uint32_t storedDay = app_nvs::readUInt(kNvsNsWifi, kNvsKeyWifiRecDay, 0U);
    uint8_t n = 0U;
    if (storedDay == day) {
        n = app_nvs::readUChar(kNvsNsWifi, kNvsKeyWifiRecRest, 0U);
    }
    if (n < 255U) {
        ++n;
    }
    (void)app_nvs::writeUInt(kNvsNsWifi, kNvsKeyWifiRecDay, day);
    (void)app_nvs::writeUChar(kNvsNsWifi, kNvsKeyWifiRecRest, n);
}

} // namespace

void wlanRecoveryServiceLoop() {
    static WlanRecoveryState s_recovery{};

    if (!s_wifiSetupComplete.load(std::memory_order_acquire) || !s_bootWifiSettled.load(std::memory_order_acquire) ||
        s_epdRefreshActive.load(std::memory_order_acquire)) {
        return;
    }

    const bool apMode = g_apMode.load(std::memory_order_relaxed);
    const bool connected = wlanStaConnectedOk();
    const bool hasCreds = s_activeWlanConfig.ssid[0] != '\0';
    const bool otaBlock = otaBlocksDestructiveAction();
    const unsigned long nowMs = millis();
    const uint8_t restartsUsed = recoveryRestartsUsedToday();

    const WlanRecoveryAction action = wlanRecoveryDecide(apMode, connected, otaBlock, hasCreds, nowMs, nowMs, s_recovery,
                                                         restartsUsed, kWlanRecoveryMaxRestartsPerDay);

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL >= 2
    const unsigned long downFor = (s_recovery.linkDownSinceMs != 0UL) ? (nowMs - s_recovery.linkDownSinceMs) : 0UL;
#endif

    switch (action) {
    case WlanRecoveryAction::None:
        break;
    case WlanRecoveryAction::ForcedReassoc:
        ESP_LOGW(TAG, "WLAN recovery action=ForcedReassoc downFor=%lu ms otaBlock=%d restarts=%u", downFor, otaBlock ? 1 : 0,
                 static_cast<unsigned>(restartsUsed));
        wlanForceStaReassoc("recovery");
        break;
    case WlanRecoveryAction::Restart:
        ESP_LOGW(TAG, "WLAN recovery action=Restart downFor=%lu ms otaBlock=%d restarts=%u", downFor, otaBlock ? 1 : 0,
                 static_cast<unsigned>(restartsUsed));
        recoveryNoteRestart();
        webServerEnd();
        wlanControlledRestart("recovery-prolonged-outage");
        break;
    }
}
