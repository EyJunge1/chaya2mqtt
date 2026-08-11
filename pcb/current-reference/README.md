# current-reference — Waveshare COTS hardware

Authoritative representation of the **currently deployed** hardware as a KiCad
reference schematic (not a production-ready carrier layout).

## Assemblies

1. **U1** Waveshare Universal e-Paper Raw Panel Driver Board (ESP32, SKU 15823) as a module
2. **DISP1** GDEH0154Z90 / Waveshare 1.54″ e-Paper (B), **SSD1681** controller, 200×200 BWR, 24-pin 0.5 mm FPC
3. **SW1** illuminated button → GPIO2 (pressed = 3.3 V / HIGH, internal pull-down)
4. **LED1** + **R1** 220 Ω–1 kΩ from GPIO4 to GND (active HIGH)
5. **USB** 5 V supply through the Waveshare board

## GPIO ↔ Firmware (`pins_esp32_waveshare.h`)

| Net | GPIO | Function |
|------|------|----------|
| BTN | 2 | Button |
| BTN_LED | 4 | Button LED |
| EPD_BUSY | 25 | Busy |
| EPD_RST | 26 | Reset |
| EPD_DC | 27 | Data/Command |
| SPI_MISO | 12 | SPI MISO (reserved in layout) |
| SPI_SCK | 13 | SPI SCK |
| SPI_MOSI | 14 | SPI MOSI |
| SPI_CS | 15 | SPI CS |

Sources: `docs/HARDWARE.md`, Waveshare Wiki *E-Paper ESP32 Driver Board*, Waveshare *1.54inch e-Paper (B) Specification* (FPC pinout Table 5-1).
