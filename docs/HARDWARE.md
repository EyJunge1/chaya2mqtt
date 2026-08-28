# Hardware

Single source for the one supported device and **everything on it**. No other board, no PCB, no CAD.

Source: [Waveshare ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G).

## Board

| Property | Value |
|----------|-------|
| **Manufacturer** | Waveshare |
| **Product** | ESP32-S3-ePaper-1.54G |
| **SKU** | **34586** (includes 3.7 V LiPo) |
| **Shop** | [esp32-s3-epaper-1.54g.htm](https://www.waveshare.com/esp32-s3-epaper-1.54g.htm) |
| **SoC** | ESP32-S3-PICO-1-N8R8, dual-core Xtensa LX7 @ 240 MHz, 512 KB SRAM, 384 KB ROM, 8 MB Flash, 8 MB **OPI** PSRAM (GPIO33–37 reserved) |
| **Wireless** | 2.4 GHz Wi-Fi (802.11 b/g/n), Bluetooth 5 (LE); **onboard antenna** (no U.FL / external connector) |
| **USB** | Type-C, native USB (GPIO19/20); also charges the cell |
| **Battery** | **3.7 V Li-ion** in the box (SKU 34586); MX1.25 2-pin; fits in the snap-fit case |
| **Enclosure** | Snap-fit case; cell installs inside |

SKU **34585** (`-EN`) is the same PCB **without** a cell in the box — not a supported target.

## Onboard inventory

What the PCB actually has. **Chaya** is the firmware use today.

| Part | Hardware | Chaya |
|------|----------|-------|
| **E-paper** | 1.54″, 200×200, **black / white / red / yellow** | Heart (red) + RX/TX counters; yellow low-battery icon |
| **Antenna** | Onboard SMD / ceramic; Wi-Fi + BLE | Wi-Fi on; BLE unused (firmware may disable BT) |
| **BOOT** (`Key1`) | GPIO0, side button | Press = send; hold during reset only for USB download mode |
| **Reset** | `EN` / `CHIP_PU` only — **no** user RESET button | USB re-plug or chip EN |
| **Li-ion cell** | 3.7 V, MX1.25 2-pin, in-case (SKU 34586) | Supported power source (with USB-C) |
| **PWR** (`BAT_KEY`) | GPIO18, side button | Press to start on battery; hold ~2 s = red shutdown screen, then soft-off (latch LOW). Short press ignored. |
| **Battery latch** | GPIO17 (`BAT_Control`) | Must drive HIGH early or power dies when PWR is released |
| **Charge IC** | ETA6098 (USB-C → cell) | No firmware |
| **Charge LED** | Onboard status LED | Hardware only |
| **Battery ADC** | GPIO4, divider; VBAT = VADC × 2 | Polled ~30 s; `GET /api/device` + SSE `device` + E-Ink icon |
| **3.3 V rail** | MP1605 DC-DC | Hardware only |
| **User LED** | GPIO3 on header, active-low | TX sequence + pulse during E-Ink refresh / RX ack; off via `cfg/led_en` |
| **TF / microSD** | Slot, **1-bit** SDIO (CLK/CMD/DAT0), **FAT32** | Permanently off (GPIO 39/40/41 held LOW; no SDIO/FAT) |
| **UART0** | GPIO43 TX / GPIO44 RX on header | Unused (debug if needed) |
| **Audio codec** | ES8311 (I2C `0x18`) | Playback only (synthetic TX/RX click). Capture path off at boot. |
| **Microphone** | Onboard, via ES8311 | Permanently off (ADC/mic-bias disabled; no I2S-RX) |
| **Speaker** | Onboard + MX1.25 2-pin header | Short click on send/receive |
| **Amp** | `PA_EN` GPIO42 (LOW = audio power on), `PA_CTRL` GPIO46 (HIGH = amp on while playing) | Same active-low power pattern as EPD_PWR |
| **Temp / humidity** | SHTC3 (I2C `0x70`) | Unused |
| **RTC** | PCF85063 (I2C `0x51`, INT GPIO5), backside | Unused |
| **Touch** | Pins reserved (`EPD_TP_RST` GPIO7, `EPD_TP_INT` GPIO21) | **No** touch panel on this SKU |
| **Expansion** | 2×6 2.54 mm header | Unused |

**Not on this board:** LoRa / Meshtastic radio, GPS, camera, extra antenna jack, user RESET key, touch glass (pins only).

I2C bus **GPIO47 (SDA) / GPIO48 (SCL)** is shared by RTC, SHTC3, ES8311, and reserved touch. Extra I2C devices must not use addresses `0x18`, `0x51`, `0x70`.

## Display

| Property | Value |
|----------|-------|
| **Size / pixels** | 1.54″, 200 × 200 |
| **Colors** | Black, white, **red**, **yellow** (4-color; 2 grey levels) |
| **Mode** | Reflective, viewing angle >170° |
| **Full refresh** | ~20 s |
| **Fast refresh** | ~15 s |
| **Interface** | SPI |
| **Power enable** | GPIO6 (`EPD3V3_EN`) — **active-low** (drive LOW to power the panel) |
| **Driver** | [GxEPD2](https://github.com/ZinggJM/GxEPD2) `GxEPD2_4C` / `GxEPD2_154c_GDEM0154F51H` |

| Signal | GPIO |
|--------|------|
| EPD3V3_EN | **6** |
| EPD_BUSY | **8** |
| EPD_RST | **9** |
| EPD_DC | **10** |
| EPD_CS | **11** |
| EPD_SCLK | **12** |
| EPD_SDI | **13** |

The heart stays **red**. Yellow is used for fresh-RX dots and a low-battery icon. Geometry and refresh logic: [DISPLAY.md](DISPLAY.md).

## Audio (playback and capture)

Onboard **ES8311** (low-power codec): mic in, speaker out. Waveshare ships an onboard speaker plus an MX1.25 header for an external speaker.

| Signal | GPIO | Role |
|--------|------|------|
| I2S_MCLK | 14 | Master clock |
| I2S_SCLK | 15 | Bit clock |
| I2S_ASDOUT | 16 | Codec → MCU (capture) |
| I2S_LRCK | 38 | Frame clock |
| I2S_DSDIN | 45 | MCU → codec (playback) |
| PA_EN | 42 | Audio power enable (**LOW** = on; Waveshare `Audio_PWR_PIN`) |
| PA_CTRL | 46 | Amplifier enable (**HIGH** while playing) |

Chaya plays a short synthetic click on heart send/receive (mute, volume, and quiet hours in settings). The microphone / capture path is disabled at boot and never used.

## TF / microSD

Onboard **TF slot**, **1-bit** SDIO (three GPIOs only). Waveshare: format as **FAT32**. Typical use: images, files, factory XiaoZhi assets.

| Signal | GPIO |
|--------|------|
| SD_CLK | 39 |
| SD_MISO | 40 |
| SD_MOSI | 41 |

Permanently off at boot (same idea as the microphone): no SDMMC/SDIO driver, no FAT mount. `sdHoldOff()` drives CLK / DAT0 / CMD **OUTPUT LOW** so the lines do not float or leave a card controller waiting. Pins: `src/hw/pins_esp32_waveshare.h`.

## Sensors and clock

| Chip | Bus | Address / pin | What it does | Chaya |
|------|-----|---------------|--------------|-------|
| **SHTC3** | I2C 47/48 | `0x70` | Ambient temperature and humidity | Unused |
| **PCF85063** | I2C 47/48 | `0x51`, INT **GPIO5** | RTC, 32.768 kHz crystal (backside) | Unused |

## Buttons

Shop text: both side buttons are programmable.

| Part | GPIO | Chaya role |
|------|------|------------|
| **BOOT** / `Key1` | **0** | Heart: press = send. Holding during reset/flash selects download mode. Strapping pin; usable as input after boot. |
| **PWR** / `BAT_KEY` | **18** | Start on battery; hold ~2 s = shutdown screen followed by soft-off/deep sleep. Short press unused in this firmware. |

## Battery and power

SKU **34586** includes a **3.7 V** single-cell Li-ion on an **MX1.25 2-pin** header. The cell fits in the snap-fit case. Waveshare does not publish mAh for the bundled pack.

| Part | Detail | Chaya |
|------|--------|-------|
| **Cell** | 3.7 V Li-ion, in-box with 34586 | Primary portable power |
| **Connector** | MX1.25 2-pin (Waveshare also labels the charge path GH1.25) | Plug in; no code |
| **Charge** | ETA6098 from USB-C; [FAQ](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G/FAQ): ~30 min to 4.1 V, ~44 min full | Automatic charge termination and recharge; no firmware control |
| **Charge LED** | Onboard | Hardware only |
| **3.3 V** | MP1605 DC-DC (`VCC3V3`) | Hardware only |
| **USB-C** | Power, charge, flash, serial | Same as desktop use |
| **BAT_KEY** | GPIO**18** | Press PWR to start on battery. Hold ~2 s after boot for the controlled shutdown; active-low deep-sleep wake on USB. |
| **BAT_Control** | GPIO**17** | Drive **HIGH early in `setup()`** or the board cuts power when PWR is released. Controlled shutdown drives it LOW only after the E-Ink shutdown screen completes. |
| **BAT_ADC** | GPIO**4**, R21/R38 200 kΩ divider | `VBAT = VADC × 2`; firmware always treats the pack as present. |

On battery there is **no** hardware hold switch: PWR is sense, GPIO17 is the latch. With USB
attached, GPIO17 cannot remove USB power, so controlled shutdown enters ESP32 deep sleep after
cutting the battery latch. Releasing and pressing PWR wakes the ESP32 and boot asserts GPIO17 again.

Waveshare low-power FAQ (factory examples, ~1 mA): ~4 days waking every 60 s, ~10 days if left in low-power. **Chaya keeps Wi-Fi up**, so those numbers do not apply — expect hours, not days.

## Expansion header

2×6, 2.54 mm female: `5V` / `3V3` / `GND`, I2C 47/48, UART0 43/44, USB 19/20, GPIO1–3.

Do **not** use GPIO19/20 as GPIO (USB). GPIO33–37 are OPI PSRAM. GPIO0 is BOOT; `EN`/`CHIP_PU` is reset — not general inputs. GPIO3 on the header is also a strapping pin (JTAG select) and the user LED.

PCB outline: Waveshare [dimensions drawing](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) (image on that page). Case is snap-fit around the board + cell.

## What Waveshare does not publish

- Bundled cell **mAh**
- Exact case outer size in text (drawing only)
- Audio amp IC part number (only `PA_EN` / `PA_CTRL`)

## Flash / OTA

The chip has **8 MB** flash. Dual OTA uses `partitions_chaya_8mb.csv` (~3.75 MB per slot). Core dump read-back must use `--chip esp32s3`.

## Operating conditions

| Aspect | Assumption |
|--------|------------|
| Power | USB-C and/or the included LiPo |
| Battery life | Always-on Wi-Fi (Chaya) drains the cell; not a multi-day badge |
| Environment | Indoors, about 0–40 °C, dry |
| Outdoor / Meshtastic | Not specified |

## USB recovery

If OTA/boot fails and the device is unreachable:

### Browser (web flasher)

1. Open the [web flasher](https://eyjunge1.github.io/chaya2mqtt/) in Chrome, Edge, or another Chromium browser with Web Serial. For local development, see [flasher/README.md](../flasher/README.md).
2. Connect USB-C with a **data** cable (hold **BOOT** only if the port will not enumerate)
3. Choose Stable or Beta and install; prefer erase on a first/recovery flash
4. Without Wi-Fi credentials the `Chaya2MQTT` SoftAP returns; scan the WIFI QR on the display or open `http://4.3.2.1/`

The flasher writes `firmware.factory.bin` (bootloader + partitions + app). OTA on a running device still uses `firmware.bin` only.

### PlatformIO

1. Connect USB-C (hold **BOOT** only if the port will not enumerate)
2. Flash a production build with `make upload-erase ENV=esp32s3-release` (erases the complete flash first). Omit `ENV=esp32s3-release` only when a debug build is intended.
3. Without Wi-Fi credentials the `Chaya2MQTT` SoftAP returns; the display shows a WIFI QR for phone camera join

On battery, press **PWR** to start; firmware must keep GPIO17 HIGH.

## Firmware implementation

`src/hw/pins_esp32_waveshare.h` and the `esp32s3` / `esp32s3-release` environments target this 1.54G only (`board = esp32-s3-devkitc1-n8r8`, OPI PSRAM, USB CDC). Product hardware is this board only.

## Further documentation

- Display geometry: [DISPLAY.md](DISPLAY.md)
- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- OTA / recovery: [OTA.md](OTA.md)
- Waveshare FAQ / datasheets / design files: [Resources](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G/Resources-And-Documents)
- Schematic PDF: [Hardware/schematics](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54G/tree/main/Hardware/schematics) (Waveshare names the file Touch; this SKU has no touch glass)
- Vendor examples / factory firmware (XiaoZhi, not Chaya): [waveshareteam/ESP32-S3-ePaper-1.54G](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54G)
