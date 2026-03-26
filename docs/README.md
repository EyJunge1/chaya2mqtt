# Heart ESP32 – E-Ink-Herz mit MQTT

Zwei ESP32-Geräte mit einem 3-Farben-E-Paper-Display zeigen jeweils ein **rotes Herz** mit einer **Zahl** (Zähler). Wird auf **Gerät A** der Knopf gedrückt, sendet es eine MQTT-Nachricht auf sein **Sende-Topic**; **Gerät B** empfängt diese auf seinem **Empfangs-Topic**, erhöht den angezeigten Zähler und aktualisiert das Display. Umgekehrt genauso – jedes Gerät hat getrennte Sende- und Empfangs-Topics, damit sich nur der Counter auf dem **anderen** Gerät erhöht.

## Funktionen

- **E-Ink-Display**: Rotes Herz mit schwarzer Zahl auf dem Waveshare 1.54inch e-Paper (B), 200x200, 3-Farben (GxEPD2, Treiber `GxEPD2_154_Z90c`)
- **MQTT-Synchronisation**: Publish beim Knopfdruck, Subscribe empfängt und erhöht den Counter
- **TLS**: Verbindung zum Broker über `WiFiClientSecure` mit dem **eingebauten Mozilla-CA-Bundle** des ESP-IDF (Zertifikatsprüfung; Port typisch **8883**)
- **Web-/Captive-Portal**: WiFi-Zugang und MQTT-Daten (Server, Port, Benutzer, Passwort, Topic) über **WiFiManager** („HeartESP32-Setup“)
- **Knopf mit LED**: Kurzer Druck → MQTT senden + LED blinkt; nach erfolgreichem Senden nochmals Blinken
- **Factory Reset**: Knopf **5 Sekunden** halten → WLAN- und MQTT-Einstellungen löschen, Neustart
- **Energieeffizienz**: CPU **80 MHz**, **WiFi Modem Sleep** plus **`WIFI_PS_MAX_MODEM`** nach Verbindung, **adaptiver Light-Sleep** in der Hauptschleife (kürzer während der Sende-LED-Sequenz, länger im Leerlauf), E-Paper-Controller **`hibernate()`** nach jedem Zeichnen (Bild bleibt sichtbar)

## Voraussetzungen

- [PlatformIO](https://platformio.org/) (CLI oder IDE-Integration)
- **Waveshare e-Paper ESP32 Driver Board** (SKU 15823) – ESP32 ist auf dem Board integriert
- **Waveshare 1.54inch e-Paper (B)** – 200x200 px, 3-Farben (schwarz/weiß/rot), ca. 8 s Full Refresh
- Beleuchteter Taster + Verkabelung (siehe [HARDWARE.md](HARDWARE.md))
- MQTT-Broker mit TLS und gültigem Server-Zertifikat von einer **öffentlichen CA** (Let's Encrypt, DigiCert, …), die im Mozilla-CA-Store enthalten ist (Bundle ist im ESP-IDF Framework eingebaut)

## Schnellstart

```bash
# Im Projektverzeichnis
pio run                    # Bauen
pio run -t upload          # Flashen
pio device monitor         # Serieller Monitor (115200 Baud)
```

Falls `pio: command not found`: PlatformIO-Core liegt unter `~/.platformio/penv/bin/pio`. Entweder **`export PATH="$HOME/.platformio/penv/bin:$PATH"`** in die Shell-Konfiguration (z. B. `~/.zshrc`) eintragen, oder im Repo **`make`** / **`make build`** nutzen (ruft dieselbe `pio`-Binary auf).

## Ersteinrichtung (WiFi & MQTT)

1. Gerät mit Strom versorgen bzw. nach Flash neu starten.
2. Wenn kein gespeichertes WLAN vorhanden ist (oder nach Reset), öffnet der ESP32 den Access Point **`HeartESP32-Setup`**.
3. Mit dem Handy/PC mit diesem AP verbinden – Captive Portal oder Browser öffnen, typisch `192.168.4.1`.
4. **WLAN** (SSID/Passwort) und folgende **MQTT-Felder** eintragen:
   - **MQTT Server** (Hostname oder IP)
   - **MQTT Port** (Standard im Code: **8883**)
   - **MQTT Username** / **MQTT Password** (leer lassen, falls nicht nötig)
   - **MQTT Sende-Topic** (Default: `heart/to_b`) – Topic, auf das bei Knopfdruck publiziert wird
   - **MQTT Empfangs-Topic** (Default: `heart/to_a`) – Topic, das abonniert wird und bei Nachricht den Counter erhöht
5. Speichern. Nach Verbindung mit dem Heim-WLAN erscheint die lokale IP im Serial Monitor.

**Hinweis:** Portal-Timeout: **180 Sekunden** (siehe `config.cpp`). Bei fehlgeschlagener Konfiguration: Neustart.

## Zwei Geräte koppeln

Beide flashen mit dem **gleichen Firmware-Stand** (empfohlen). Dieselben **Broker-Zugangsdaten** eintragen (WLAN kann dasselbe oder ein anderes sein – wichtig ist Erreichbarkeit des Brokers). Die **Topics müssen gekreuzt** konfiguriert werden:

| | Gerät A | Gerät B |
|---|---------|---------|
| **Sende-Topic** | `heart/to_b` | `heart/to_a` |
| **Empfangs-Topic** | `heart/to_a` | `heart/to_b` |

So gilt: Knopf auf A → publiziert auf `heart/to_b` → B empfängt → **Counter auf B wird um 1 erhöht** und Herz neu gezeichnet. Umgekehrt genauso (siehe [ARCHITECTURE.md](ARCHITECTURE.md)).

## Factory Reset

Knopf **mindestens 5 Sekunden** gedrückt halten → `resetAllSettings()` löscht WiFiManager-Daten und den Preferences-Namespace `mqtt`, anschließend **Neustart**. Danach wieder Captive Portal.

## Projektstruktur (Firmware)

```
heart-esp32/
├── platformio.ini
├── docs/
│   ├── README.md          # Diese Datei
│   ├── ARCHITECTURE.md    # Architektur & Datenfluss
│   ├── HARDWARE.md        # Pins & Hardware
│   └── MODULES.md         # Code-Referenz
└── src/
    ├── main.cpp
    ├── config.cpp / config.h
    ├── display.cpp / display.h
    ├── mqtt.cpp / mqtt.h
    └── button.cpp / button.h
```

## Abhängigkeiten (PlatformIO)

Definiert in `platformio.ini`:

| Bibliothek | Zweck |
|------------|--------|
| **GxEPD2** | E-Paper-Treiber |
| **Adafruit GFX / BusIO** | Grafik-Primitives für das Display |
| **PubSubClient** | MQTT-Client |
| **WiFiManager** | WLAN-Konfiguration + Custom-Parameter für MQTT |

## Weitere Dokumentation

- [ARCHITECTURE.md](ARCHITECTURE.md) – Module, Ablauf, MQTT
- [HARDWARE.md](HARDWARE.md) – Pinbelegung und Komponenten
- [MODULES.md](MODULES.md) – Funktionen, Globals, Implementierungsdetails
