# Hardware

## Hauptplatine: Waveshare e-Paper ESP32 Driver Board

| Eigenschaft | Wert |
|-------------|------|
| **Hersteller** | Waveshare |
| **Produkt** | Universal e-Paper Raw Panel Driver Board, ESP32 WiFi / Bluetooth Wireless |
| **SKU** | 15823 |
| **SoC** | ESP32 (onboard) – WiFi 802.11 b/g/n, Bluetooth 4.2 |
| **Betriebsspannung** | 5 V (USB) |
| **Betriebsstrom** | 50–150 mA |
| **Low-Power-Strom** | < 2 mA (CP2102 abgeschaltet) |
| **SPI-Interface** | 4-Wire SPI |
| **Abmessungen** | 29,46 × 48,25 mm |

Das Board hat den **ESP32 direkt integriert** – kein separates DevKit nötig. Das E-Paper-Panel wird über den SPI-Header angeschlossen.

> **Wiki:** [E-Paper_ESP32_Driver_Board](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board)

## Display: 1.54" e-Paper (B)

| Eigenschaft | Wert |
|-------------|------|
| **Modell** | Waveshare 1.54" e-Paper (B) |
| **Controller** | SSD1682 |
| **Farben** | Schwarz, Weiß, **Rot** (3-Farben / BWR) |
| **Auflösung** | 200 × 200 Pixel |
| **Aktive Fläche** | 27,60 × 27,60 mm |
| **Full Refresh** | ca. **8–14 s** (Treiber: `kFullRefreshMs = 14000`) |
| **Partial Refresh** | Nicht unterstützt (3-Farben-Variante) |
| **Interface** | SPI |
| **Treiber** | Projekteigener `EpdDriver154Z90c` (abgeleitet von GxEPD2_154_Z90c) |

## Pinbelegung

Alle GPIO-Zuweisungen sind zentral in **`src/hw/pins.h`** (`namespace pins`) definiert.

> **Hinweis:** GPIO 6–11 sind mit dem Flash verbunden und dürfen nicht verwendet werden.

### SPI & Display

| Signal | GPIO | Funktion |
|--------|------|----------|
| SCK | **13** | SPI-Takt |
| MISO | **12** | SPI MISO |
| MOSI | **14** | SPI Daten zum Display |
| CS | **15** | Chip Select Display |
| DC | **27** | Data/Command |
| RST | **26** | Reset |
| BUSY | **25** | Busy-Status (Input) |

Initialisierung: `SPI.begin(13, 12, 14, 15)`.

Nach jedem Draw wird SPI low-power gehalten (`gpio_hold` auf CS).

### Taster & LED

| Funktion | GPIO | Modus |
|----------|------|-------|
| **Button** | **2** | `INPUT_PULLDOWN` – offen = LOW, gedrückt = HIGH |
| **Button-LED** | **4** | `OUTPUT` – HIGH = LED an |

Der Taster ist ein **beleuchteter Knopf** – die LED wird über GPIO 4 angesteuert und dient als visuelles Feedback:

| Situation | LED-Verhalten |
|-----------|---------------|
| Startup | 3× 200 ms Blink |
| MQTT senden | 2× Blink → Publish → 2× Blink (Erfolg) oder 3× Blink (Fehler) |
| Web-Auth | Langsamer Blink während Challenge |
| Factory Reset | Blinkmuster bei 10 s Halten |

## EPD-Treiber

Der Treiber liegt unter `src/display/epd/` und ist **keine PlatformIO-Dependency**:

| Datei | Inhalt |
|-------|--------|
| `epd_driver.h` / `epd_driver.cpp` | `EpdDriver154Z90c` – SPI-Kommunikation, Refresh, Hibernate |
| `epd_colors.h` | `GxEPD_BLACK`, `GxEPD_WHITE`, `GxEPD_RED` |

Abgeleitet von GxEPD2 (Vendor-Code), aber als **eigenständiger, getrimmter Treiber** eingebunden:
- Nur Full-Window-Refresh (kein Partial Update)
- Paging über `firstPage()` / `nextPage()`
- `hibernate()` setzt Controller in Deep Sleep (Bild bleibt bistabil)

Build-Flag: `-Isrc/display/epd` in `platformio.ini`.

## Verkabelungshinweise

- **Stromversorgung:** 5 V über USB am Driver Board
- **Display:** Direkt über E-Paper-Header (Flachbandkabel / Pin-Header)
- **Taster + LED:** Extern an GPIO 2 (Button) und GPIO 4 (LED)
- **LED-Vorwiderstand:** Typisch 220 Ω–1 kΩ
- **Refresh-Dauer:** Jeder Full Refresh dauert ca. 8–14 s – das Display flackert kurz bei jedem Update

## Flash-Partition

Dual-OTA-Layout (`huge_app.csv`):

| Partition | Größe | Zweck |
|-----------|-------|-------|
| `nvs` | 20 KB | Non-Volatile Storage |
| `otadata` | 8 KB | OTA-Boot-Auswahl |
| `ota_0` | 1,875 MB | App-Slot A |
| `ota_1` | 1,875 MB | App-Slot B |
| `coredump` | 64 KB | Core-Dump |
| `spiffs` | 128 KB | SPIFFS (optional) |

## Abgleich mit älterer Doku

Frühere Notizen verwendeten teils **GDEW0154Z04** oder **GxEPD2 als PIO-Dependency**. Die aktuelle Firmware nutzt:
- Panel: **1.54" e-Paper (B)** mit Controller **SSD1682**
- Treiber: **`EpdDriver154Z90c`** (projekteigen, nicht GxEPD2-Library)

Beim Panel-Austausch Treiber und Pin-Zuordnung prüfen.

## Weitere Dokumentation

- Display-Geometrie: [DISPLAY.md](DISPLAY.md)
- Architektur: [ARCHITECTURE.md](ARCHITECTURE.md)
