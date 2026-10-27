#pragma once

/** Pin map for Waveshare ESP32-S3-ePaper-1.54G (SKU 34586). */
namespace pins {

constexpr int kButton = 0;    // BOOT / Key1 (active-low)
constexpr int kButtonLed = 3; // Header user LED (active-low)

constexpr int kDisplayPwrEn = 6; // EPD3V3_EN: drive LOW to power panel
/** Touch reset — no touch on 1.54G (never); forced LOW+hold via touchHoldOff(). */
constexpr int kEpdTpRst = 7;
constexpr int kDisplayBusy = 8;
constexpr int kDisplayRst = 9;
constexpr int kDisplayDc = 10;

constexpr int kSpiMosi = 13; // EPD_SDI
constexpr int kSpiSck = 12;  // EPD_SCLK
constexpr int kSpiCs = 11;   // EPD_CS
/** No EPD MISO on this board; pass -1 to SPI.begin. */
constexpr int kSpiMiso = -1;

constexpr int kRtcInt = 5;      // PCF85063 INT — unused; INPUT_PULLDOWN via touchHoldOff()
constexpr int kBatControl = 17; // Drive HIGH early or LiPo power cuts on PWR release
constexpr int kBatAdc = 4;      // Divider; VBAT = VADC × 2
constexpr int kPwrButton = 18;  // PWR / BAT_KEY (soft-off long press)
/** Touch interrupt — no touch on 1.54G (never); forced LOW+hold via touchHoldOff(). */
constexpr int kEpdTpInt = 21;

constexpr int kI2cSda = 47;
constexpr int kI2cScl = 48;

constexpr int kI2sMclk = 14;
constexpr int kI2sSclk = 15;
/** Codec→ESP mic data; capture unused — held with other I2S lines when rail off. */
constexpr int kI2sAsdout = 16;
constexpr int kI2sLrck = 38;
constexpr int kI2sDsdin = 45;
/** PA_EN HIGH = PAVCC off (MOSFET): codec + mic + amp unpowered. */
constexpr int kPaEn = 42;
constexpr int kPaCtrl = 46; // Amp enable: HIGH while playing

/**
 * TF / microSD 1-bit SDIO (CLK / DAT0 / CMD). No SD VDD switch on this PCB.
 * Never started as SDMMC; sdHoldOff() holds lines OUTPUT LOW.
 */
constexpr int kSdClk = 39;
constexpr int kSdMiso = 40;
constexpr int kSdMosi = 41;

} // namespace pins
