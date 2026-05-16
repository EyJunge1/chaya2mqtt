# Chaya2MQTT – E-Ink-Herz mit MQTT

Zwei ESP32-Geräte mit einem 3-Farben-E-Paper-Display zeigen jeweils ein **rotes Herz** mit einer **Zahl** (Zähler). Wird auf **Gerät A** der Knopf gedrückt, publiziert es seinen aktuellen Sendezähler als **retained MQTT-Nachricht** auf sein **Sende-Topic**; **Gerät B** empfängt diese auf seinem **Empfangs-Topic**, setzt seinen Zähler auf den empfangenen Wert und aktualisiert das Display. Umgekehrt genauso – jedes Gerät hat getrennte Sende- und Empfangs-Topics, damit sich nur der Counter auf dem **anderen** Gerät ändert. Dank retained Messages holt sich ein Gerät nach einer Offline-Phase automatisch den aktuellen Zählerstand, sobald es wieder verbindet.

## Funktionen

- **E-Ink-Display**: Rotes Herz mit schwarzer Zahl auf dem Waveshare 1.54inch e-Paper (B), 200x200, 3-Farben (GxEPD2, Treiber `GxEPD2_154_Z90c`)
- **MQTT-Synchronisation**: Publish beim Knopfdruck (Sendezähler als retained Dezimalstring); Subscribe setzt den Counter auf den empfangenen Wert – verpasste Nachrichten werden beim Reconnect automatisch nachgeholt
- **TLS**: Verbindung zum Broker über `WiFiClientSecure` mit dem **eingebauten Mozilla-CA-Bundle** des ESP-IDF (Zertifikatsprüfung; Port typisch **8883**)
- **Web-/Captive-Portal**: WiFi-Zugang über SoftAP **`Chaya2MQTT`** und Browser (`AsyncWebServer`); MQTT und weitere Einstellungen unter **http://chaya2mqtt.local** nach STA-Verbindung
- **Knopf mit LED**: Kurzer Druck → MQTT senden + LED blinkt; nach erfolgreichem Senden nochmals Blinken
- **Factory Reset**: Knopf **10 Sekunden** halten → alle NVS-Namespaces (WLAN, MQTT, Zähler, Einstellungen) löschen, Neustart
- **Energieeffizienz**: CPU **80 MHz**, **WiFi Modem Sleep** plus **`WIFI_PS_MIN_MODEM`** nach STA-Verbindung, **adaptiver Light-Sleep** in der Hauptschleife (kürzer bei aktiver Sende-LED-Sequenz / MQTT-Backoff), E-Paper **`hibernate()`**

## Voraussetzungen

- [PlatformIO](https://platformio.org/) (CLI oder IDE-Integration)
- **Waveshare e-Paper ESP32 Driver Board** (SKU 15823) – ESP32 ist auf dem Board integriert
- **Waveshare 1.54inch e-Paper (B)** – 200x200 px, 3-Farben (schwarz/weiß/rot), ca. 8 s Full Refresh
- Beleuchteter Taster + Verkabelung (siehe [HARDWARE.md](HARDWARE.md))
- MQTT-Broker mit TLS und gültigem Server-Zertifikat von einer **öffentlichen CA** (Let's Encrypt, DigiCert, …), die im Mozilla-CA-Store enthalten ist (Bundle ist im ESP-IDF Framework eingebaut)

## Schnellstart

```bash
# Im Projektverzeichnis
pio run                    # Bauen (Debug: CORE_DEBUG_LEVEL=3)
pio run -e esp32dev-release   # Optional: Produktion ohne Serial-Debug-Ausgaben
pio run -t upload          # Flashen
pio device monitor         # Serieller Monitor (115200 Baud)
```

Falls `pio: command not found`: PlatformIO-Core liegt unter `~/.platformio/penv/bin/pio`. Entweder **`export PATH="$HOME/.platformio/penv/bin:$PATH"`** in die Shell-Konfiguration (z. B. `~/.zshrc`) eintragen, oder im Repo **`make`** / **`make build`** nutzen (ruft dieselbe `pio`-Binary auf).

## Ersteinrichtung (WiFi & MQTT)

1. Gerät mit Strom versorgen bzw. nach Flash neu starten.
2. Wenn kein gespeichertes WLAN vorhanden ist (oder nach Reset), öffnet der ESP32 den Access Point **`Chaya2MQTT`**.
3. Mit dem Handy/PC mit diesem AP verbinden – Captive Portal oder Browser öffnen, typisch `4.3.2.1`.
4. **WLAN** (SSID/Passwort) und folgende **MQTT-Felder** eintragen:
   - **MQTT Server** (Hostname oder IP)
   - **MQTT Port** (Standard im Code: **8883**)
   - **MQTT Username** / **MQTT Password** (leer lassen, falls nicht nötig)
   - **MQTT Sende-Topic** (Default: `chaya/to_b`) – Topic, auf das bei Knopfdruck publiziert wird
   - **MQTT Empfangs-Topic** (Default: `chaya/to_a`) – Topic, das abonniert wird und bei Nachricht den Counter setzt
5. Speichern. Nach Verbindung mit dem Heim-WLAN erscheint die lokale IP im Serial Monitor (Debug-Build).

## Zwei Geräte koppeln

Beide flashen mit dem **gleichen Firmware-Stand** (empfohlen). Dieselben **Broker-Zugangsdaten** eintragen (WLAN kann dasselbe oder ein anderes sein – wichtig ist Erreichbarkeit des Brokers). Die **Topics müssen gekreuzt** konfiguriert werden:

| | Gerät A | Gerät B |
|---|---------|---------|
| **Sende-Topic** | `chaya/to_b` | `chaya/to_a` |
| **Empfangs-Topic** | `chaya/to_a` | `chaya/to_b` |

So gilt: Knopf auf A → publiziert Sendezähler (retained) auf `chaya/to_b` → B empfängt → **Counter auf B wird auf diesen Wert gesetzt** und Herz neu gezeichnet. War B offline, holt es sich den aktuellen Stand beim nächsten Reconnect. Umgekehrt genauso (siehe [ARCHITECTURE.md](ARCHITECTURE.md)).

## Factory Reset

Knopf **mindestens 10 Sekunden** gedrückt halten → `resetAllSettings()` (**wlan**) löscht die NVS-Namespaces `wifi`, `mqtt`, `cfg` und `chaya`, setzt RAM-Zähler zurück und startet neu. Danach wieder SoftAP **`Chaya2MQTT`** bei fehlenden STA-Credentials.

## Projektstruktur (Firmware)

```
chaya2mqtt/
├── README.md              # Kurzer Einstieg, verweist hierher
├── platformio.ini
├── docs/
│   ├── README.md          # Diese Datei
│   ├── ARCHITECTURE.md    # Architektur & Datenfluss
│   ├── HARDWARE.md        # Pins & Hardware
│   └── MODULES.md         # Code-Referenz
└── src/
    ├── main.cpp
    ├── counter.cpp / counter.h     # Zähler & Baselines NVS
    ├── constants.h
    ├── version.h
    ├── tls_bundle.h
    ├── hw/                           # Hardware
    │   ├── button.cpp / button.h    # Taster + LED
    │   ├── display.cpp / display.h  # E-Paper (GxEPD2)
    │   └── pins.h                   # GPIO-Zuordnung (namespace pins)
    ├── net/                         # WLAN, MQTT, OTA (Name wlan ≠ Arduino WiFi.h)
    │   ├── wlan.cpp / wlan.h
    │   ├── wifi_test.cpp / wifi_test.h
    │   ├── mqtt.cpp / mqtt.h
    │   ├── mqtt_config.cpp / mqtt_config.h
    │   └── ota.cpp / ota.h          # Firmware-Update / GitHub-Check
    ├── config/
    │   ├── app_config.cpp / app_config.h
    │   └── nvs_utils.h
    └── web/                         # HTTP-Admin-GUI (ESPAsyncWebServer)
        ├── admin.cpp / admin.h      # Routen, webAdminLoop
        ├── auth.cpp / auth.h        # Sessions, CSRF
        ├── pages.cpp / pages.h      # HTML-Streaming
        ├── web_utils.cpp / web_utils.h
        └── assets/                  # eingebettetes CSS/JS (PROGMEM Header)
            ├── styles.h
            └── wifi_scan_js.h       # u. a.; weitere *_js.h
```

## Abhängigkeiten (PlatformIO)

Definiert in `platformio.ini`:

| Bibliothek | Zweck |
|------------|--------|
| **Adafruit GFX / BusIO** | Grafik-Primitives für das E-Paper-Panel |
| **ESP-IDF MQTT** (`esp_mqtt_client`, über Arduino-ESP32 eingebunden) | MQTT über TLS (**keine** zusätzliche PubSubClient-Library) |
| **ESPAsyncWebServer** | HTTP-Admin-Oberfläche + Captive-Portal-Modus |
| **ArduinoJson** | GitHub-Release-JSON (`tag_name`) fuer OTA-Versionscheck |

(E-Paper: projekteigener Treiber unter `src/display/epd/`, nicht GxEPD2.)

## Weitere Dokumentation

- [ARCHITECTURE.md](ARCHITECTURE.md) – Module, Ablauf, MQTT
- [HARDWARE.md](HARDWARE.md) – Pinbelegung und Komponenten
- [MODULES.md](MODULES.md) – Funktionen, Globals, Implementierungsdetails
