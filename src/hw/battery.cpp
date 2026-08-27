#include "battery.h"

#include "battery_config.h"
#include "battery_pure.h"
#include "pins.h"

#include <Arduino.h>
#include <atomic>
#include <driver/gpio.h>
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
    analogSetPinAttenuation(static_cast<uint8_t>(pins::kBatAdc), ADC_11db);
    int sum = 0;
    for (int i = 0; i < kBatteryAdcSamples; ++i) {
        sum += static_cast<int>(analogReadMilliVolts(static_cast<uint8_t>(pins::kBatAdc)));
    }
    const int pinMv = sum / kBatteryAdcSamples;
    return pinMv * kBatteryDividerRatio;
}

}  // namespace

void batteryInit() {
    pinMode(pins::kBatAdc, INPUT);
    analogSetPinAttenuation(static_cast<uint8_t>(pins::kBatAdc), ADC_11db);
    batteryPoll();
}

void batteryPoll() {
    const int mv  = readPackMilliVolts();
    const int pct = batteryPctFromMilliVolts(mv);
    s_batteryMv.store(mv, std::memory_order_relaxed);
    s_batteryPct.store(pct, std::memory_order_relaxed);
    ESP_LOGD(TAG, "VBAT %d mV (%d%%)", mv, pct);
}

int batteryMilliVolts() {
    return s_batteryMv.load(std::memory_order_relaxed);
}

int batteryPercent() {
    return s_batteryPct.load(std::memory_order_relaxed);
}

void batteryPowerOffAndSleep() {
    const gpio_num_t pwrGpio = static_cast<gpio_num_t>(pins::kPwrButton);
    (void)rtc_gpio_pulldown_dis(pwrGpio);
    (void)rtc_gpio_pullup_en(pwrGpio);
    const esp_err_t wakeErr =
        esp_sleep_enable_ext1_wakeup_io(1ULL << pins::kPwrButton, ESP_EXT1_WAKEUP_ANY_LOW);
    if (wakeErr != ESP_OK) {
        ESP_LOGE(TAG, "Cannot arm PWR wake: %s", esp_err_to_name(wakeErr));
        ESP.restart();
        for (;;) {
        }
    }

    ESP_LOGI(TAG, "Power-off: latch LOW, USB fallback deep sleep");
    gpio_hold_dis(static_cast<gpio_num_t>(pins::kBatControl));
    digitalWrite(pins::kBatControl, LOW);
    pinMode(pins::kBatControl, OUTPUT);
    digitalWrite(pins::kBatControl, LOW);
    esp_deep_sleep_start();
}
