# Hardware

## Main board: Waveshare e-Paper ESP32 Driver Board

| Property | Value |
|----------|-------|
| **Manufacturer** | Waveshare |
| **Product** | Universal e-Paper Raw Panel Driver Board, ESP32 WiFi / Bluetooth Wireless |
| **SKU** | 15823 |
| **SoC** | ESP32 (onboard) – WiFi 802.11 b/g/n, Bluetooth 4.2 |
| **Operating voltage** | 5 V (USB) |
| **Operating current** | 50–150 mA |
| **Low-power current** | < 2 mA (CP2102 disabled) |
| **SPI-Interface** | 4-Wire SPI |
| **Dimensions** | 29.46 × 48.25 mm |

The board has an **integrated ESP32**—no separate DevKit is required. The e-paper panel connects through the SPI header.

> **Wiki:** [E-Paper_ESP32_Driver_Board](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board)

## Display: 1.54" e-Paper (B)

| Property | Value |
|----------|-------|
| **Model** | Waveshare 1.54" e-Paper (B) |
| **Controller** | SSD1682 |
| **Colors** | Black, white, **red** (3-color / BWR) |
| **Resolution** | 200 × 200 pixels |
| **Active area** | 27.60 × 27.60 mm |
| **Full refresh** | Approx. **8–14 s** |
| **Partial refresh** | Not supported (3-color variant) |
| **Interface** | SPI |
| **Driver** | [GxEPD2](https://github.com/ZinggJM/GxEPD2) (`GxEPD2_154_Z90c` + `GxEPD2_3C`) |

## Pinout

All GPIO assignments are defined centrally in **`src/hw/pins.h`** (`namespace pins`).

> **Note:** GPIO 6–11 are connected to flash and must not be used.

### SPI & Display

| Signal | GPIO | Function |
|--------|------|----------|
| SCK | **13** | SPI clock |
| MISO | **12** | SPI MISO (assigned by the Waveshare layout; the EPD typically requires only MOSI) |
| MOSI | **14** | SPI data to the display |
| CS | **15** | Chip Select Display |
| DC | **27** | Data/Command |
| RST | **26** | Reset |
| BUSY | **25** | Busy status (input) |

Initialization: `SPI.begin(13, 12, 14, 15)`.

After each draw, SPI is kept in low-power mode (`gpio_hold` on CS).

### Button & LED

| Function | GPIO | Mode |
|----------|------|------|
| **Button** | **2** | `INPUT_PULLDOWN`—open = LOW, pressed = HIGH |
| **Button LED** | **4** | `OUTPUT`—HIGH = LED on |

The button is **illuminated**—its LED is controlled through GPIO 4 and provides visual feedback:

| Situation | LED behavior |
|-----------|--------------|
| Startup | Blink 3× for 200 ms |
| MQTT send | Blink 2× → publish → blink 2× (success) or 3× (error) |
| Factory reset | Blink pattern after holding for 10 s |

### Wiring diagram (button + LED)

The firmware expects the button to switch **3.3 V → GPIO 2** (pressed = HIGH). The ESP32's internal pull-down is used; no external pull-up is required. Connect the LED from GPIO 4 through a series resistor to GND (active-HIGH drive).

```text
                 Waveshare ESP32 Driver Board
                 ┌──────────────────────────┐
     USB 5 V ────┤ 5V / USB                 │
                 │                          │
           3V3 ──┤ 3V3                      │
                 │                     GPIO2├────┬──── Button ──── 3V3
                 │                          │    │
                 │                          │   (internal pull-down)
                 │                          │
                 │                     GPIO4├────[220Ω–1kΩ]──── LED+ → LED− → GND
                 │                          │
            GND ─┤ GND                      │
                 └──────────────────────────┘
```

**Recommendations:**
- LED series resistor: typically **220 Ω–1 kΩ** (depending on the LED / brightness)
- Use short wires; in an enclosure, use JST-XH or equivalent connectors instead of loose stranded wire
- A shared GND reference between the button, LED, and board is mandatory

### Available / reserved GPIOs

| GPIO | Status |
|------|--------|
| 6–11 | **Reserved** (flash)—do not use |
| 2, 4 | Assigned (button / LED) |
| 12–15, 25–27 | Assigned (display SPI) |
| 0, 5, 16–19, 21–23, 32–33, 34–39 | Theoretically available on the ESP32—**check the Waveshare header** before connecting sensors |

Bluetooth is intentionally disabled in the firmware (`btStop()`).

## Operating conditions

| Aspect | Assumption |
|--------|------------|
| Operation | Always on in a home WiFi network, 5 V USB |
| Environment | Indoors, room temperature (approx. 0–40 °C), dry air |
| Enclosure | Optional; keep the display and USB accessible |
| Outdoor / battery | **Not specified**—not designed for deep sleep |

## EPD driver

The display is controlled by the PlatformIO library **`ZinggJM/GxEPD2`** (currently 1.6.x):

| Component | Details |
|-----------|---------|
| Panel driver | `GxEPD2_154_Z90c`—SSD1682 / GDEH0154Z90, SPI, refresh, hibernate |
| 3-color wrapper | `GxEPD2_3C<…>`—paging through `firstPage()` / `nextPage()` |
| Project alias | `ChayaEpdPanel` in `src/display/internal.h` |

Firmware behavior:
- Full-window refresh (no partial update with the 3-color panel)
- `hibernate()` puts the controller into deep sleep (the image remains bistable)
- Colors: `GxEPD_BLACK`, `GxEPD_WHITE`, `GxEPD_RED` (from GxEPD2)
- In the setup AP, the splash screen shows the SSID and `http://4.3.2.1`; the setup AP is open

## Wiring notes

- **Power supply:** 5 V through USB on the driver board
- **Display:** Directly through the e-paper header (ribbon cable / pin header)
- **Button + LED:** Externally connected to GPIO 2 (button) and GPIO 4 (LED)—see the diagram above
- **Refresh duration:** Each full refresh takes approximately 8–14 s—the display briefly flickers with every update

## Flash partitions

Dual-OTA-Layout (`huge_app.csv`):

| Partition | Size | Purpose |
|-----------|------|---------|
| `nvs` | 20 KB | Non-Volatile Storage |
| `otadata` | 8 KB | OTA boot selection |
| `ota_0` | 1,875 MB | App-Slot A |
| `ota_1` | 1,875 MB | App-Slot B |
| `coredump` | 64 KB | Core dump |
| `spiffs` | 128 KB | SPIFFS (currently unused) |

Local core dump analysis: `python3 scripts/analyze_coredump.py <dump.bin>`—see [OTA.md](OTA.md)#usb-recovery--core-dumps.

## Brick recovery (USB)

If OTA/boot fails and the device is no longer reachable through WiFi:

1. Connect USB and note the port
2. Optionally erase flash: `pio run -e esp32dev-release -t erase`
3. Flash the release again: `make upload ENV=esp32dev-release`
4. Serial monitor: `make monitor` (decoder as configured in `platformio.ini`)
5. Without stored WiFi credentials, the open `Chaya2MQTT` SoftAP reappears; the display shows the SSID and setup URL/IP

## PCB documentation (`pcb/`)

| Path | Content |
|------|---------|
| [`pcb/current-reference/`](../pcb/current-reference/) | KiCad reference schematic for the current Waveshare COTS hardware (pin map = `pins_esp32_waveshare.h`) |
| [`pcb/chaya2mqtt-s2/`](../pcb/chaya2mqtt-s2/) | Unreleased design for possible future hardware |

The firmware currently supports only the Waveshare hardware. Future hardware has not yet been determined; designs and outstanding checks therefore remain under [`pcb/`](../pcb/) and are not approved production targets.

The Gaggimate PCB serves only as a source of ideas (CC BY-NC-SA 4.0); mains-voltage sections are not reused.

## Further documentation

- Display geometry: [DISPLAY.md](DISPLAY.md)
- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- OTA / Recovery: [OTA.md](OTA.md)
- PCB: [`pcb/README.md`](../pcb/README.md)
