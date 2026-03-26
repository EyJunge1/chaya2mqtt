# Hardware

## Hauptplatine: Waveshare e-Paper ESP32 Driver Board

| Eigenschaft | Wert |
|-------------|------|
| **Hersteller** | Waveshare |
| **Produkt** | Universal e-Paper Raw Panel Driver Board, ESP32 WiFi / Bluetooth Wireless |
| **SKU** | 15823 |
| **Part No.** | e-Paper ESP32 Driver Board |
| **SoC** | ESP32 (onboard) – WiFi 802.11 b/g/n, Bluetooth 4.2 (BR/EDR + BLE) |
| **Betriebsspannung** | 5 V (USB) |
| **Betriebsstrom** | 50–150 mA |
| **Low-Power-Strom** | < 2 mA (CP2102 abgeschaltet) |
| **SPI-Interface** | 4-Wire SPI (Standard) |
| **Abmessungen** | 29,46 × 48,25 mm |

Das Board hat den **ESP32 direkt integriert** – es ist kein separates DevKit nötig. Das E-Paper-Panel wird über den SPI-Header des Boards angeschlossen.

> **Wiki / Datenblatt:** [E-Paper_ESP32_Driver_Board](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board)

## Display: 1.54inch e-Paper (B)

| Eigenschaft | Wert |
|-------------|------|
| **Modell** | Waveshare 1.54inch e-Paper (B) |
| **Farben** | Schwarz, Weiß, **Rot** (3-Farben) |
| **Auflösung** | 200 × 200 Pixel |
| **Aktive Fläche** | 27,60 × 27,60 mm |
| **Full Refresh** | ca. **8 Sekunden** |
| **Partial Refresh** | Nicht unterstützt (3-Farben-Variante) |
| **Interface** | SPI |
| **GxEPD2-Treiber** | `GxEPD2_154_Z90c` |

## Pinbelegung (wie im Code)

### SPI & Display (GxEPD2)

| Signal | GPIO | Funktion |
|--------|------|----------|
| SCK | **13** | SPI-Takt |
| MISO | **12** | SPI MISO |
| MOSI | **14** | SPI Daten zum Display |
| CS | **15** | Chip Select Display |
| DC | **27** | Data/Command |
| RST | **26** | Reset |
| BUSY | **25** | Busy-Status |

`SPI.begin(13, 12, 14, 15)` entspricht SCK, MISO, MOSI, SS(CS).

### Taster & LED

| Funktion | GPIO | Modus im Code |
|----------|------|----------------|
| **Button** | **2** | `INPUT_PULLDOWN` – offen = LOW, gedrückt (nach 3,3 V) = `HIGH` |
| **Button-LED** | **4** | `OUTPUT`, `HIGH` = LED an |

Der Taster ist ein **beleuchteter Knopf** – die LED im Taster wird über GPIO 4 angesteuert und dient als visuelles Feedback (Blinken bei Senden, Startsequenz).

## Verkabelungshinweise

- **Stromversorgung:** 5 V über USB am Driver Board; das Board versorgt Display und ESP32.
- **Display:** Wird direkt über den E-Paper-Header des Driver Boards verbunden (Flachbandkabel / Pin-Header – je nach Panel-Typ).
- **Taster + LED:** Extern angeschlossen an GPIO 2 (Button) und GPIO 4 (LED). Vorwiderstand für die LED beachten (typ. 220 Ω–1 kΩ).
- **Refresh-Dauer:** Da das 3-Farben-Panel ca. 8 s für einen Full Refresh braucht und **kein** Partial Refresh unterstützt, flackert das Display bei jedem `drawHeartWithNumber()`-Aufruf kurz.

## Abgleich mit älterer Doku

Frühere Notizen verwendeten teils **GDEW0154Z04** – die Firmware nutzt explizit **`GxEPD2_154_Z90c`** (passend zum 1.54inch e-Paper (B) Panel). Beim Austausch des Panels den GxEPD2-Treiber und die Pin-Zuordnung prüfen.

Weitere Software-Details: [ARCHITECTURE.md](ARCHITECTURE.md), [MODULES.md](MODULES.md).
