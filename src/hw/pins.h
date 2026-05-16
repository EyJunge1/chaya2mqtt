#pragma once

/** Board GPIO assignments (ESP32 + GxEPD2 + button/LED). Pins 6–11 are flash — do not use. */
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

}  // namespace pins
