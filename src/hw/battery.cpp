#include "battery.h"

#include "battery_config.h"
#include "battery_pure.h"
#include "pins.h"

#include <Arduino.h>
#include <atomic>
#include <driver/gpio.h>
#include <esp_log.h>

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

void batterySoftOff() {
    ESP_LOGI(TAG, "Soft-off: latch LOW");
    gpio_hold_dis(static_cast<gpio_num_t>(pins::kBatControl));
    digitalWrite(pins::kBatControl, LOW);
    pinMode(pins::kBatControl, OUTPUT);
    digitalWrite(pins::kBatControl, LOW);
}
