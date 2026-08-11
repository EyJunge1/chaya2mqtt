# current-reference — Waveshare COTS-Hardware

Autoritative Abbildung der **jetzt eingesetzten** Hardware als KiCad-Referenzschaltplan
(kein fertigungsfähiges Carrier-Layout).

## Baugruppen

1. **U1** Waveshare Universal e-Paper Raw Panel Driver Board (ESP32, SKU 15823) als Modul
2. **DISP1** GDEH0154Z90 / Waveshare 1.54″ e-Paper (B), Controller **SSD1681**, 200×200 BWR, FPC 24-Pin 0,5 mm
3. **SW1** beleuchteter Taster → GPIO2 (gedrückt = 3,3 V / HIGH, interner Pull-down)
4. **LED1** + **R1** 220 Ω–1 kΩ von GPIO4 nach GND (aktive HIGH)
5. **USB** 5 V Versorgung über das Waveshare-Board

## GPIO ↔ Firmware (`pins_esp32_waveshare.h`)

| Netz | GPIO | Funktion |
|------|------|----------|
| BTN | 2 | Taster |
| BTN_LED | 4 | Taster-LED |
| EPD_BUSY | 25 | Busy |
| EPD_RST | 26 | Reset |
| EPD_DC | 27 | Data/Command |
| SPI_MISO | 12 | SPI MISO (Layout-reserviert) |
| SPI_SCK | 13 | SPI SCK |
| SPI_MOSI | 14 | SPI MOSI |
| SPI_CS | 15 | SPI CS |

Quellen: `docs/HARDWARE.md`, Waveshare Wiki *E-Paper ESP32 Driver Board*, Waveshare *1.54inch e-Paper (B) Specification* (FPC-Pinout Tabelle 5-1).
