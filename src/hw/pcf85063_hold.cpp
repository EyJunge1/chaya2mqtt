#include "hw/pcf85063_hold.hpp"

#include <Wire.h>
#include <cstdint>
#include <esp_log.h>

#include "util/log_tag.hpp"

DEFINE_LOG_TAG("RTC");

namespace {
constexpr uint8_t kPcf85063I2cAddr = 0x51;
constexpr uint8_t kRegControl1 = 0x00;
constexpr uint8_t kRegControl2 = 0x01;
constexpr uint8_t kRegTimerMode = 0x11;
/** Control_1 STOP bit — RTC divider chain stopped. */
constexpr uint8_t kControl1Stop = 0x20;
/** Control_2: alarms/flags clear, COF=111 → CLKOUT held LOW. */
constexpr uint8_t kControl2ClkoutLow = 0x07;
} // namespace

namespace {
bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(kPcf85063I2cAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}
} // namespace

void pcf85063Stop() {
    if (!writeReg(kRegControl2, kControl2ClkoutLow)) {
        ESP_LOGW(TAG, "Control_2 NACK — leaving as-is if present");
        return;
    }
    (void)writeReg(kRegTimerMode, 0x00); // TE off
    if (!writeReg(kRegControl1, kControl1Stop)) {
        ESP_LOGW(TAG, "STOP NACK");
        return;
    }
    ESP_LOGI(TAG, "clock stopped; CLKOUT low (unused RTC)");
}
