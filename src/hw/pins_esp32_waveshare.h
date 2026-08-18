#pragma once

/** Pin map for Waveshare ESP32-S3-ePaper-1.54G (SKU 34586); see docs/HARDWARE.md. */
namespace pins {

constexpr int kButton    = 0;  // BOOT / Key1 (active-low)
constexpr int kButtonLed = 3;  // Header user LED (active-low)

constexpr int kDisplayPwrEn = 6;  // EPD3V3_EN: drive LOW to power panel
constexpr int kDisplayBusy  = 8;
constexpr int kDisplayRst   = 9;
constexpr int kDisplayDc    = 10;

constexpr int kSpiMosi = 13;  // EPD_SDI
constexpr int kSpiSck  = 12;  // EPD_SCLK
constexpr int kSpiCs   = 11;  // EPD_CS
/** No EPD MISO on this board; pass -1 to SPI.begin. */
constexpr int kSpiMiso = -1;

constexpr int kBatControl = 17;  // Drive HIGH early or LiPo power cuts on PWR release

/** Board identity for compile-time diagnostics. */
constexpr const char* kBoardId = "esp32s3-epaper-154g";

}  // namespace pins
