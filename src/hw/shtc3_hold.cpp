#include "hw/shtc3_hold.hpp"

#include <Wire.h>
#include <cstdint>
#include <esp_log.h>

#include "util/log_tag.hpp"

DEFINE_LOG_TAG("SHTC3");

namespace {
constexpr uint8_t kShtc3I2cAddr = 0x70;
/** Sensirion sleep command 0xB098 — idle ~45 µA → sleep ~0.3 µA. */
constexpr uint8_t kShtc3SleepCmdHi = 0xB0;
constexpr uint8_t kShtc3SleepCmdLo = 0x98;
} // namespace

void shtc3Sleep() {
    Wire.beginTransmission(kShtc3I2cAddr);
    Wire.write(kShtc3SleepCmdHi);
    Wire.write(kShtc3SleepCmdLo);
    const uint8_t err = Wire.endTransmission();
    if (err != 0U) {
        ESP_LOGW(TAG, "sleep NACK/err=%u — leaving idle if present", static_cast<unsigned>(err));
        return;
    }
    ESP_LOGI(TAG, "sleep (~0.3uA); no VDD switch on PCB");
}
