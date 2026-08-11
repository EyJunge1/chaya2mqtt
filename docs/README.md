# Chaya2MQTT – E-Ink-Herz mit MQTT

Zwei ESP32-Geräte mit einem **3-Farben-E-Paper-Display** zeigen jeweils ein **rotes Herz** mit **Zählerständen**. Wird auf **Gerät A** der Knopf gedrückt, publiziert es den nächsten Sendezähler als **retained MQTT-Nachricht** auf sein **Sende-Topic**; **Gerät B** empfängt diese auf seinem **Empfangs-Topic**, setzt seinen Zähler auf den empfangenen Wert und aktualisiert das Display. Umgekehrt genauso – jedes Gerät hat getrennte Sende- und Empfangs-Topics, damit sich nur der Counter auf dem **anderen** Gerät ändert.

Dank **retained Messages** holt sich ein Gerät nach einer Offline-Phase automatisch den aktuellen Zählerstand, sobald es wieder verbindet.

## Funktionen

| Bereich | Beschreibung |
|---------|--------------|
| **E-Ink-Display** | Rotes Herz mit RX/TX-Zähler (Delta-Anzeige), Waveshare 1.54" e-Paper (B), 200×200, 3-Farben |
| **MQTT-Sync** | Publish beim Knopfdruck oder per Web-UI; Subscribe setzt Counter auf empfangenen Wert |
| **TLS** | Verbindung zum Broker über `mqtts://` mit **Mozilla-CA-Bundle** (Port **8883**) |
| **Web-Admin** | Captive Portal im AP-Modus, danach `http://chaya2mqtt.local` |
| **Pairing** | Device-ID aus MAC → automatische Topics `chaya2mqtt/<id>` |
| **Knopf + LED** | Kurzdruck → MQTT senden; 10 s Halten → Factory Reset |
| **OTA** | Automatischer GitHub-Release-Check (täglich) + manueller Trigger |

## Hardware-Ziel

- **Board:** Waveshare e-Paper ESP32 Driver Board (SKU 15823)
- **Display:** Waveshare 1.54" e-Paper (B), 200×200, schwarz/weiß/rot
- **Taster + LED:** GPIO 2 (Button), GPIO 4 (LED)

Details: [HARDWARE.md](HARDWARE.md)

Tests und Qualitätsgates: [TESTING.md](TESTING.md)

## Voraussetzungen

- [PlatformIO](https://platformio.org/) (CLI oder IDE)
- MQTT-Broker mit TLS und gültigem Server-Zertifikat einer **öffentlichen CA** (Let's Encrypt, DigiCert, …)
- Beleuchteter Taster mit Vorwiderstand für die LED

## Schnellstart

```bash
# Im Projektverzeichnis
pio run                      # Bauen (Debug: CORE_DEBUG_LEVEL=3)
pio run -e esp32dev-release    # Produktion ohne Serial-Debug
pio run -t upload            # Flashen
pio device monitor             # Serieller Monitor (115200 Baud)
```

Alternativ über das Makefile:

```bash
make build          # Debug bauen
make upload-release # Release flashen
make monitor        # Serial-Monitor
```

Falls `pio: command not found`: PlatformIO liegt unter `~/.platformio/penv/bin/pio`. Entweder `export PATH="$HOME/.platformio/penv/bin:$PATH"` setzen oder `make` nutzen.

## Ersteinrichtung (WiFi & MQTT)

1. Gerät mit Strom versorgen bzw. nach Flash neu starten.
2. Ohne gespeichertes WLAN öffnet der ESP32 den offenen Access Point **`Chaya2MQTT`**. Das E-Ink-Display zeigt SSID und Setup-URL.
3. Mit Handy/PC verbinden – Captive Portal oder Browser öffnen (typisch `http://4.3.2.1`).
4. **WLAN** (SSID/Passwort) eintragen. MQTT wird nach dem Wechsel in den STA-Modus unter **MQTT** konfiguriert:
   - **MQTT Server** (Hostname oder IP)
   - **MQTT Port** (Standard: **8883**)
   - **MQTT Username / Password** (optional)
   - **Partner-ID** des anderen Geräts (6 Hex-Zeichen)
5. Im AP-Modus wird die WLAN-Verbindung **getestet**, bevor Credentials gespeichert werden.
6. Nach erfolgreicher Verbindung: Admin-UI unter **`http://chaya2mqtt.local`**.

## Zwei Geräte koppeln

Beide Geräte mit dem **gleichen Firmware-Stand** flashen. Broker-Zugangsdaten müssen identisch sein (WLAN kann unterschiedlich sein).

1. Auf beiden Geräten die Seite **MQTT** öffnen (`/mqtt`).
2. Denselben Broker eintragen.
3. Die eigene **Device-ID** (6 Hex-Zeichen aus der MAC) notieren und auf dem anderen Gerät als **Partner-ID** speichern – und umgekehrt.
4. Topics werden automatisch gesetzt:
   - **Sende-Topic:** `chaya2mqtt/<eigene_id>` (z. B. `chaya2mqtt/a1b2c3`)
   - **Empfangs-Topic:** `chaya2mqtt/<partner_id>` (z. B. `chaya2mqtt/f5e6d7`)

So können **mehrere Paare** denselben Broker nutzen, ohne Topic-Kollisionen. Ohne Partner bleibt das Gerät am Broker, abonniert aber kein Geräte-Topic.

Details: [MQTT.md](MQTT.md)

## Factory Reset

Knopf **mindestens 10 Sekunden** gedrückt halten → alle NVS-Namespaces (`wifi`, `mqtt`, `cfg`, `chaya`) werden gelöscht, Neustart. Danach wieder der offene SoftAP **`Chaya2MQTT`**.

## Projektstruktur

```
chaya2mqtt/
├── README.md                 # Kurzer Einstieg
├── platformio.ini            # Build-Konfiguration
├── huge_app.csv              # Dual-OTA (Waveshare / 4 MB)
├── Makefile                  # pio-Wrapper
├── docs/                     # Diese Dokumentation
├── pcb/                      # Aktuelle Hardware-Referenz und künftige Entwürfe
└── src/
    ├── main.cpp              # Bootstrap, Task-Start
    ├── constants.h           # Geräteweite Identity, NTP, Syntax-Validierung
    ├── async/                # Queues, Mutexe, App-Task
    ├── config/               # app_config, nvs_utils, version.h
    ├── diag/                 # Stack-Monitor, Task-WDT
    ├── display/              # EPD (GxEPD2) + Draw + Display-Task
    ├── heart/                # Zähler, Baselines, NVS
    ├── hw/                   # Button und Pins der Waveshare-Hardware
    ├── mqtt/                 # Config + Client/Events/Publish/Reconnect
    ├── network/              # network_task (WLAN + MQTT Loop)
    ├── ota/                  # GitHub-Check, Flash-Install, Health-Gate
    ├── tls/                  # CA-Bundle (MQTT + OTA)
    ├── util/                 # Zeit-Helfer, Logging, IP-Format
    ├── web/                  # Admin-API + SPA-Serving (routes/, assets/, csrf)
    └── wifi/                 # WLAN, Captive Portal, Recovery, Verbindungstest
frontend/                     # React-19 SPA (Vite, Tailwind, Lucide) + Mock-Gerät
```

## Build-Umgebungen

| Umgebung | Zweck | Debug-Level | Optimierung |
|----------|-------|-------------|-------------|
| `esp32dev` | Waveshare-Entwicklung | `CORE_DEBUG_LEVEL=3` | Standard |
| `esp32dev-release` | Waveshare-Produktion (Default) | `CORE_DEBUG_LEVEL=0` | `-Os`, `-DNDEBUG` |

Partition Waveshare: **Dual OTA** (`huge_app.csv`) – je Slot ca. 1,875 MB.

## Abhängigkeiten (PlatformIO)

| Bibliothek | Zweck |
|------------|--------|
| **GxEPD2** | E-Paper-Treiber (`GxEPD2_154_Z90c`, 3-Farben-Paging) |
| **Adafruit GFX / BusIO** | Grafik-Primitives für E-Paper |
| **ESP-IDF MQTT** (`esp_mqtt_client`) | MQTT über TLS (kein PubSubClient) |
| **ESPAsyncWebServer** | HTTP-Admin + Captive Portal |

## Dokumentation

| Datei | Inhalt |
|-------|--------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | FreeRTOS-Tasks, Queues, Datenflüsse |
| [MODULES.md](MODULES.md) | Code-Referenz aller Module |
| [MQTT.md](MQTT.md) | Protokoll, Topics, TLS, Pairing |
| [WEB_ADMIN.md](WEB_ADMIN.md) | HTTP-Routen, CSRF, SSE |
| [openapi.yaml](openapi.yaml) | REST-API (OpenAPI 3.1) |
| [asyncapi.yaml](asyncapi.yaml) | SSE-Events (AsyncAPI 3) |
| [HARDWARE.md](HARDWARE.md) | Board, Display, Pinbelegung |
| [OTA.md](OTA.md) | Firmware-Updates über GitHub |
| [CONFIGURATION.md](CONFIGURATION.md) | NVS-Namespaces, Defaults |
| [DISPLAY.md](DISPLAY.md) | Display-Task, Herz-Geometrie, Delta-Logik |
| [SECURITY.md](SECURITY.md) | Threat Model, Empfehlungen |

## Lizenz

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — Weiterentwicklung und Teilen mit Namensnennung erlaubt, **keine kommerzielle Nutzung**. Volltext: [LICENSE](../LICENSE).
