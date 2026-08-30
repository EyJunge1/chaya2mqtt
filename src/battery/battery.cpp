#include "battery.h"

#include "battery_config.h"
#include "battery_pure.h"
#include "async/sse_dirty.h"
#include "hw/pins.h"

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
bool             s_adcAttenuationApplied{false};

int readPackMilliVolts() {
    const auto pin = static_cast<uint8_t>(pins::kBatAdc);
    // Arduino ADC: first analogReadMilliVolts configures the channel; attenuation
    // before that logs "Pin is not configured as analog channel".
    if (!s_adcAttenuationApplied) {
        (void)analogReadMilliVolts(pin);
        analogSetPinAttenuation(pin, ADC_11db);
        s_adcAttenuationApplied = true;
    }
    int sum = 0;
    for (int i = 0; i < kBatteryAdcSamples; ++i) {
        sum += static_cast<int>(analogReadMilliVolts(pin));
    }
    const int pinMv = sum / kBatteryAdcSamples;
    return pinMv * kBatteryDividerRatio;
}

}  // namespace

void batteryInit() {
    batteryPoll();
}

void batteryPoll() {
    const int mv  = readPackMilliVolts();
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

    ESP_LOGI(TAG, "deep sleep arm wake=PWR mv=%d pct=%d", batteryMilliVolts(), batteryPercent());
    gpio_hold_dis(static_cast<gpio_num_t>(pins::kBatControl));
    digitalWrite(pins::kBatControl, LOW);
    pinMode(pins::kBatControl, OUTPUT);
    digitalWrite(pins::kBatControl, LOW);
    esp_deep_sleep_start();
}
