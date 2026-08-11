#pragma once

/** Waveshare ESP32 e-Paper Driver Board + 1.54" BWR panel (current COTS hardware). */
namespace pins {

constexpr int kButton    = 2;
constexpr int kButtonLed = 4;

constexpr int kDisplayBusy = 25;
constexpr int kDisplayRst  = 26;
constexpr int kDisplayDc   = 27;

constexpr int kSpiMiso = 12;
constexpr int kSpiMosi = 14;
constexpr int kSpiSck  = 13;
constexpr int kSpiCs   = 15;

/** Board identity for compile-time diagnostics. */
constexpr const char* kBoardId = "esp32-waveshare";

}  // namespace pins
