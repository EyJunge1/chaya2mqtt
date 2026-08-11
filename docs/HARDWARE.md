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
| **Full Refresh** | ca. **8–14 s** |
| **Partial Refresh** | Nicht unterstützt (3-Farben-Variante) |
| **Interface** | SPI |
| **Treiber** | [GxEPD2](https://github.com/ZinggJM/GxEPD2) (`GxEPD2_154_Z90c` + `GxEPD2_3C`) |

## Pinbelegung

Alle GPIO-Zuweisungen sind zentral in **`src/hw/pins.h`** (`namespace pins`) definiert.

> **Hinweis:** GPIO 6–11 sind mit dem Flash verbunden und dürfen nicht verwendet werden.

### SPI & Display

| Signal | GPIO | Funktion |
|--------|------|----------|
| SCK | **13** | SPI-Takt |
| MISO | **12** | SPI MISO (vom Waveshare-Layout belegt; EPD braucht typisch nur MOSI) |
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
| Factory Reset | Blinkmuster bei 10 s Halten |

### Verdrahtungsdiagramm (Taster + LED)

Firmware erwartet: Taster schaltet **3,3 V → GPIO 2** (gedrückt = HIGH). Interner Pull-down am ESP32; kein externer Pull-up nötig. LED von GPIO 4 über Vorwiderstand nach GND (aktive HIGH-Ansteuerung).

```text
                 Waveshare ESP32 Driver Board
                 ┌──────────────────────────┐
     USB 5 V ────┤ 5V / USB                 │
                 │                          │
           3V3 ──┤ 3V3                      │
                 │                     GPIO2├────┬──── Taster ──── 3V3
                 │                          │    │
                 │                          │   (interne Pull-down)
                 │                          │
                 │                     GPIO4├────[220Ω–1kΩ]──── LED+ → LED− → GND
                 │                          │
            GND ─┤ GND                      │
                 └──────────────────────────┘
```

**Empfehlungen:**
- LED-Vorwiderstand: typisch **220 Ω–1 kΩ** (je nach LED / Helligkeit)
- Kurze Leitungen; bei Gehäuse JST-XH oder gleichwertige Stecker statt lose Litze
- Gemeinsame GND-Referenz zwischen Taster, LED und Board zwingend

### Freie / reservierte GPIOs

| GPIO | Status |
|------|--------|
| 6–11 | **Reserviert** (Flash) — nicht nutzen |
| 2, 4 | Belegt (Button / LED) |
| 12–15, 25–27 | Belegt (Display-SPI) |
| 0, 5, 16–19, 21–23, 32–33, 34–39 | Theoretisch frei am ESP32 — **Waveshare-Header prüfen**, bevor Sensoren angeschlossen werden |

Bluetooth ist firmwareseitig absichtlich deaktiviert (`btStop()`).

## Einsatzbedingungen

| Aspekt | Annahme |
|--------|---------|
| Betrieb | Always-on im Heim-WLAN, 5 V USB |
| Umgebung | Indoor, Raumtemperatur (ca. 0–40 °C), trockene Luft |
| Gehäuse | Optional; Display und USB zugänglich halten |
| Outdoor / Batterie | **Nicht spezifiziert** — kein Deep-Sleep-Design |

## EPD-Treiber

Display-Ansteuerung über die PlatformIO-Library **`ZinggJM/GxEPD2`** (aktuell 1.6.x):

| Komponente | Inhalt |
|------------|--------|
| Panel-Treiber | `GxEPD2_154_Z90c` – SSD1682 / GDEH0154Z90, SPI, Refresh, Hibernate |
| 3-Farben-Wrapper | `GxEPD2_3C<…>` – Paging über `firstPage()` / `nextPage()` |
| Projekt-Alias | `ChayaEpdPanel` in `src/display/internal.h` |

Verhalten in der Firmware:
- Full-Window-Refresh (kein Partial Update bei 3-Farben-Panel)
- `hibernate()` setzt Controller in Deep Sleep (Bild bleibt bistabil)
- Farben: `GxEPD_BLACK`, `GxEPD_WHITE`, `GxEPD_RED` (aus GxEPD2)
- Im Setup-AP zeigt der Splash SSID und `http://4.3.2.1`; der Setup-AP ist offen

## Verkabelungshinweise

- **Stromversorgung:** 5 V über USB am Driver Board
- **Display:** Direkt über E-Paper-Header (Flachbandkabel / Pin-Header)
- **Taster + LED:** Extern an GPIO 2 (Button) und GPIO 4 (LED) — siehe Diagramm oben
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
| `spiffs` | 128 KB | SPIFFS (derzeit ungenutzt) |

Core-Dump-Analyse lokal: `python3 scripts/analyze_coredump.py <dump.bin>` — siehe [OTA.md](OTA.md)#usb-recovery--core-dumps.

## Brick-Recovery (USB)

Wenn OTA/Boot fehlschlägt und das Gerät nicht mehr im WLAN erreichbar ist:

1. USB verbinden, Port notieren
2. Optional Flash löschen: `make erase` bzw. `make erase-release`
3. Release neu flashen: `make upload-release` oder `make upload-release-clean`
4. Serial-Monitor: `make monitor` (Decoder laut `platformio.ini`)
5. Ohne gespeicherte WLAN-Credentials erscheint wieder der offene SoftAP `Chaya2MQTT`; das Display zeigt SSID und Setup-URL/IP

## PCB-Dokumentation (`pcb/`)

| Pfad | Inhalt |
|------|--------|
| [`pcb/current-reference/`](../pcb/current-reference/) | KiCad-Referenzschaltplan der aktuellen Waveshare-COTS-Hardware (Pinmap = `pins_esp32_waveshare.h`) |
| [`pcb/chaya2mqtt-s2/`](../pcb/chaya2mqtt-s2/) | Nicht freigegebener Entwurf für mögliche künftige Hardware |

Die Firmware unterstützt derzeit ausschließlich die Waveshare-Hardware. Künftige Hardware ist noch nicht festgelegt; Entwürfe und offene Prüfungen bleiben deshalb unter [`pcb/`](../pcb/) und sind keine freigegebenen Produktionsziele.

Das Gaggimate-PCB dient nur als Ideenquelle (CC BY-NC-SA 4.0); Netzspannungsblöcke werden nicht übernommen.

## Weitere Dokumentation

- Display-Geometrie: [DISPLAY.md](DISPLAY.md)
- Architektur: [ARCHITECTURE.md](ARCHITECTURE.md)
- OTA / Recovery: [OTA.md](OTA.md)
- PCB: [`pcb/README.md`](../pcb/README.md)
