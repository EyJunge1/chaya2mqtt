# Hardware

Chaya unterstützt **nur** dieses Gerät. Kein eigenes PCB, kein CAD, kein zweites Board.

## Board: Waveshare ESP32-S3-ePaper-1.54G (mit Akku)

| Property | Value |
|----------|-------|
| **Manufacturer** | Waveshare |
| **Product** | ESP32-S3-ePaper-1.54G |
| **SKU** | **34586** (inkl. 3,7 V LiPo) |
| **Docs** | [ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) |
| **Shop** | [esp32-s3-epaper-1.54g.htm](https://www.waveshare.com/esp32-s3-epaper-1.54g.htm) |
| **SoC** | ESP32-S3-PICO-1-N8R8 (240 MHz, 8 MB Flash, 8 MB PSRAM) |
| **Wireless** | 2,4 GHz Wi-Fi, Bluetooth 5 (LE) |
| **USB** | Type-C, natives USB (GPIO19/20) |
| **Gehäuse** | Snap-Fit, Akku im Gehäuse |

SKU **34585** (`-EN`) ist dasselbe Board **ohne** mitgelieferten Akku — kein unterstütztes Ziel.

## Display

| Property | Value |
|----------|-------|
| **Size** | 1,54″, 200 × 200 |
| **Colors** | Black, white, **red**, **yellow** |
| **Full refresh** | ~20 s |
| **Fast refresh** | ~15 s |
| **Interface** | SPI |
| **Power enable** | GPIO6 (`EPD3V3_EN`) |

## Onboard (genutzt / nicht genutzt)

| Teil | Rolle für Chaya |
|------|-----------------|
| **BOOT** (GPIO0, `Key1`) | Heart: kurz = senden, 10 s = Factory-Reset. Beim Flash/Reset nicht gedrückt halten (Download-Modus). |
| **PWR** (GPIO18 `BAT_KEY`) | Soft-Power am Akku. Firmware muss **GPIO17** (`BAT_Control`) früh **HIGH** halten, sonst geht die Versorgung aus. |
| RTC PCF85063, SHTC3, TF, Audio, Mikro, Lautsprecher | Nicht nötig für Chaya |
| Touch (`EPD_TP_*`) | Nur reserviert, **kein** Touch-Panel |
| Header GPIO1–3 | Frei; GPIO3 ggf. User-LED |

Shop: beide Seitentaster programmierbar. Wiki: GPIO0 ist Strapping, „nicht als allgemeiner User-Input empfohlen“ — nach dem Boot trotzdem lesbar.

## Pinout (E-Paper + Taster)

Quelle: [Waveshare-Doku](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G).

| Signal | GPIO | Funktion |
|--------|------|----------|
| EPD_PWR / EPD3V3_EN | **6** | Display-Versorgung |
| EPD_BUSY | **8** | Busy |
| EPD_RST | **9** | Reset |
| EPD_DC | **10** | Data/Command |
| EPD_CS | **11** | Chip Select |
| EPD_SCLK | **12** | SPI clock |
| EPD_SDI | **13** | SPI MOSI |
| BAT_Control | **17** | Akku-Latch, boot HIGH |
| BAT_KEY / PWR | **18** | PWR-Taster |
| BOOT / Key1 | **0** | BOOT-Taster / Heart |
| BAT_ADC | **4** | Akkuspannung (VADC × 2) |

Expansion: 5 V / 3V3 / GND, I2C 47/48, UART 43/44, USB 19/20, GPIO1–3.

GPIO19/20 = USB — nicht als GPIO. GPIO33–37 = OPI-PSRAM.

## Firmware-Stand

`src/hw/pins_esp32_waveshare.h` und `esp32dev` gehören noch zum **alten** Driver-Board (SKU 15823). Der Port auf S3 + 4-Farbe-Panel (GxEPD2 4C) ist die nächste Firmware-Arbeit. Hardware-Ziel ist trotzdem nur dieses 1.54G.

## Operating conditions

| Aspect | Assumption |
|--------|------------|
| Power | USB-C und/oder mitgelieferter LiPo |
| Environment | Indoors, ca. 0–40 °C, trocken |
| Outdoor / Meshtastic | Nicht vorgesehen |

## Further documentation

- Display geometry: [DISPLAY.md](DISPLAY.md)
- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- OTA / Recovery: [OTA.md](OTA.md)
