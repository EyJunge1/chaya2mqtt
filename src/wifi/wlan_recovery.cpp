#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"
#include "wlan_recovery.h"
#include "wlan_soft_reconnect.h"

#include "async/system_lifecycle.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "ota/ota.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_timer.h>
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
    const unsigned long uptimeMs = static_cast<unsigned long>(esp_timer_get_time() / 1000LL);
    const uint8_t restartsUsed = recoveryRestartsUsedToday();

    const unsigned long lastForcedBeforeDecide = s_recovery.lastForcedReassocMs;
    const WlanRecoveryAction action = wlanRecoveryDecide(apMode, connected, otaBlock, hasCreds, nowMs, uptimeMs, s_recovery,
                                                         restartsUsed, kWlanRecoveryMaxRestartsPerDay);

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL >= 2
    const unsigned long downFor = (s_recovery.linkDownSinceMs != 0UL) ? (nowMs - s_recovery.linkDownSinceMs) : 0UL;
#endif

    switch (action) {
    case WlanRecoveryAction::None:
        break;
    case WlanRecoveryAction::ForcedReassoc:
        if (s_wifiScanInProgress.load(std::memory_order_acquire)) {
            // STAB-07 / BUG-NET-03: do not disconnect+begin while a scan owns the STA iface.
            // No pending flag — undo decide's cooldown stamp so the next loop can retry.
            s_recovery.lastForcedReassocMs = lastForcedBeforeDecide;
            ESP_LOGD(TAG, "WLAN recovery deferred (scan in progress)");
            break;
        }
        ESP_LOGW(TAG, "WLAN recovery action=ForcedReassoc downFor=%lu ms otaBlock=%d restarts=%u", downFor, otaBlock ? 1 : 0,
                 static_cast<unsigned>(restartsUsed));
        if (wlanForceCallerShouldUndo(wlanForceStaReassoc("recovery"))) {
            s_recovery.lastForcedReassocMs = lastForcedBeforeDecide;
        }
        break;
    case WlanRecoveryAction::Restart:
        if (s_epdRefreshActive.load(std::memory_order_acquire)) {
            ESP_LOGD(TAG, "WLAN recovery restart deferred (EPD)");
            break;
        }
        if (g_factoryResetQueued.load(std::memory_order_acquire) ||
            g_systemShutdownInProgress.load(std::memory_order_acquire)) {
            ESP_LOGW(TAG, "WLAN recovery restart skipped — factory or shutdown in progress");
            break;
        }
        if (otaBlocksDestructiveAction()) {
            ESP_LOGW(TAG, "WLAN recovery restart skipped — OTA in progress");
            break;
        }
        ESP_LOGW(TAG, "WLAN recovery action=Restart downFor=%lu ms otaBlock=%d restarts=%u", downFor, otaBlock ? 1 : 0,
                 static_cast<unsigned>(restartsUsed));
        // Note rec_rst only after a successful claim — ESP.restart() does not return (BUG-NET-05).
        (void)wlanControlledRestart("recovery-prolonged-outage", recoveryNoteRestart);
        break;
    }
}
