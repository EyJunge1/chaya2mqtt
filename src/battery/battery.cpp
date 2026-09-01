#include "battery.h"

#include "async/sse_dirty.h"
#include "battery_config.h"
#include "battery_pure.h"
#include "button/button_config.h"
#include "button/button_soft_off_pure.h"
#include "hw/pins.h"

#include <Arduino.h>
#include <atomic>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("BAT");

namespace {

std::atomic<int> s_batteryMv{0};
std::atomic<int> s_batteryPct{0};

int readPackMilliVolts() {
    const auto pin = static_cast<uint8_t>(pins::kBatAdc);
    int sum = 0;
    for (int i = 0; i < kBatteryAdcSamples; ++i) {
        sum += static_cast<int>(analogReadMilliVolts(pin));
    }
    return (sum / kBatteryAdcSamples) * kBatteryDividerRatio;
}

int pwrLevelForExt1(gpio_num_t pwrGpio, bool rtcPad) {
    return rtcPad ? rtc_gpio_get_level(pwrGpio) : digitalRead(pins::kPwrButton);
}

bool configurePwrRtcPad(gpio_num_t pwrGpio) {
    const esp_err_t initErr = rtc_gpio_init(pwrGpio);
    if (initErr != ESP_OK) {
        ESP_LOGW(TAG, "rtc_gpio_init PWR: %s — using digitalRead", esp_err_to_name(initErr));
        return false;
    }
    (void)rtc_gpio_pulldown_dis(pwrGpio);
    (void)rtc_gpio_pullup_en(pwrGpio);
    return true;
}

void waitPwrSettledHigh(gpio_num_t pwrGpio, bool rtcPad) {
    SoftOffReleaseSettle settle{};
    for (;;) {
        const unsigned long nowMs = millis();
        if (softOffReleaseSettled(settle, pwrLevelForExt1(pwrGpio, rtcPad), nowMs, kSoftOffReleaseSettleMs)) {
            return;
        }
        delay(10);
    }
}

void armPwrExt1Wake() {
    const esp_err_t wakeErr = esp_sleep_enable_ext1_wakeup_io(1ULL << pins::kPwrButton, ESP_EXT1_WAKEUP_ANY_LOW);
    if (wakeErr == ESP_OK) {
        return;
    }
    ESP_LOGE(TAG, "Cannot arm PWR wake: %s", esp_err_to_name(wakeErr));
    ESP.restart();
    for (;;) {
    }
}

void disarmPwrExt1Wake() { (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1); }

} // namespace

void batteryInit() { batteryPoll(); }

void batteryPoll() {
    const int mv = readPackMilliVolts();
    const int pct = batteryPctFromMilliVolts(mv);
    const int prevMv = s_batteryMv.load(std::memory_order_relaxed);
    const int prevPct = s_batteryPct.load(std::memory_order_relaxed);
    s_batteryMv.store(mv, std::memory_order_relaxed);
    s_batteryPct.store(pct, std::memory_order_relaxed);
    if (mv != prevMv || pct != prevPct) {
        sseMarkDirty(kSseDevice);
    }
    ESP_LOGD(TAG, "VBAT %d mV (%d%%)", mv, pct);
}

int batteryMilliVolts() { return s_batteryMv.load(std::memory_order_relaxed); }

int batteryPercent() { return s_batteryPct.load(std::memory_order_relaxed); }

void batteryCutLatch() {
    digitalWrite(pins::kBatControl, LOW);
    pinMode(pins::kBatControl, OUTPUT);
    digitalWrite(pins::kBatControl, LOW);
}

void batteryPowerOffAndSleep() {
    const gpio_num_t pwrGpio = static_cast<gpio_num_t>(pins::kPwrButton);
    const bool rtcPad = configurePwrRtcPad(pwrGpio);

    for (;;) {
        if (!softOffMayArmExt1Wake(pwrLevelForExt1(pwrGpio, rtcPad))) {
            ESP_LOGW(TAG, "PWR still LOW — cutting latch, waiting before EXT1 ANY_LOW");
            batteryCutLatch();
        }
        waitPwrSettledHigh(pwrGpio, rtcPad);
        armPwrExt1Wake();
        batteryCutLatch();
        // IDF: wake condition must be false at esp_deep_sleep_start()
        ESP_LOGI(TAG, "deep sleep arm wake=PWR mv=%d pct=%d", batteryMilliVolts(), batteryPercent());
        if (!softOffMayArmExt1Wake(pwrLevelForExt1(pwrGpio, rtcPad))) {
            ESP_LOGW(TAG, "PWR LOW at sleep entry — disarming EXT1, waiting");
            disarmPwrExt1Wake();
            continue;
        }
        esp_deep_sleep_start();
    }
}
