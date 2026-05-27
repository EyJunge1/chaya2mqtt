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
| **Pairing** | Device-ID aus MAC → automatische Topics `chaya/<id>` |
| **Knopf + LED** | Kurzdruck → MQTT senden; 10 s Halten → Factory Reset |
| **OTA** | Automatischer GitHub-Release-Check (täglich) + manueller Trigger |
| **Web-Auth** | Optional: 6-stelliger Code auf E-Ink + physischer Tastendruck |

## Hardware-Ziel

- **Board:** Waveshare e-Paper ESP32 Driver Board (SKU 15823)
- **Display:** Waveshare 1.54" e-Paper (B), 200×200, schwarz/weiß/rot
- **Taster + LED:** GPIO 2 (Button), GPIO 4 (LED)

Details: [HARDWARE.md](HARDWARE.md)

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
2. Ohne gespeichertes WLAN öffnet der ESP32 den Access Point **`Chaya2MQTT`**.
3. Mit Handy/PC verbinden – Captive Portal oder Browser öffnen (typisch `http://4.3.2.1`).
4. **WLAN** (SSID/Passwort) und **MQTT-Felder** eintragen:
   - **MQTT Server** (Hostname oder IP)
   - **MQTT Port** (Standard: **8883**)
   - **MQTT Username / Password** (optional)
   - **Sende-Topic** (Default: `chaya/to_b`)
   - **Empfangs-Topic** (Default: `chaya/to_a`)
5. Im AP-Modus wird die WLAN-Verbindung **getestet**, bevor Credentials gespeichert werden.
6. Nach erfolgreicher Verbindung: Admin-UI unter **`http://chaya2mqtt.local`**.

## Zwei Geräte koppeln

Beide Geräte mit dem **gleichen Firmware-Stand** flashen. Broker-Zugangsdaten müssen identisch sein (WLAN kann unterschiedlich sein).

### Empfohlen: Device-ID-Pairing

1. Auf beiden Geräten die Seite **Pairing** öffnen (`/pairing`).
2. Jedes Gerät zeigt seine **Device-ID** (6 Hex-Zeichen aus der MAC) und einen **QR-Code**.
3. Auf Gerät A die **Partner-ID** von Gerät B eintragen (oder QR scannen) – auf Gerät B analog.
4. Topics werden automatisch gesetzt:
   - **Sende-Topic:** `chaya/<eigene_id>` (z. B. `chaya/a1b2c3`)
   - **Empfangs-Topic:** `chaya/<partner_id>` (z. B. `chaya/f5e6d7`)

So können **mehrere Paare** denselben Broker nutzen, ohne Topic-Kollisionen.

Details: [MQTT.md](MQTT.md)

### Manuell (Fortgeschrittene)

Topics unter **MQTT** manuell kreuzen (löscht gespeicherte Partner-ID):

| | Gerät A | Gerät B |
|---|---------|---------|
| **Sende-Topic** | `chaya/to_b` | `chaya/to_a` |
| **Empfangs-Topic** | `chaya/to_a` | `chaya/to_b` |

## Factory Reset

Knopf **mindestens 10 Sekunden** gedrückt halten → alle NVS-Namespaces (`wifi`, `mqtt`, `cfg`, `chaya`) werden gelöscht, Neustart. Danach wieder SoftAP **`Chaya2MQTT`**.

## Projektstruktur

```
chaya2mqtt/
├── README.md                 # Kurzer Einstieg
├── platformio.ini            # Build-Konfiguration
├── huge_app.csv              # Dual-OTA-Partitionstabelle
├── Makefile                  # pio-Wrapper
├── docs/                     # Diese Dokumentation
└── src/
    ├── main.cpp              # Bootstrap, Task-Start
    ├── async/                # Queues, Mutexe, App-Task
    ├── config/               # app_config, nvs_utils
    ├── diag/                 # Stack-Monitor, Task-WDT
    ├── display/              # EPD (GxEPD2) + Draw + Display-Task
    ├── heart/                # Zähler, Baselines, NVS
    ├── hw/                   # Button, Pins
    ├── mqtt/                 # Config + esp_mqtt_client
    ├── network/              # network_task (WLAN + MQTT Loop)
    ├── ota/                  # GitHub-Check, Flash-Install
    ├── web/                  # Admin-UI, Auth, SSE, Assets
    └── wifi/                 # WLAN, Captive Portal, Verbindungstest
```

## Build-Umgebungen

| Umgebung | Zweck | Debug-Level | Optimierung |
|----------|-------|-------------|-------------|
| `esp32dev` | Entwicklung | `CORE_DEBUG_LEVEL=3` | Standard |
| `esp32dev-release` | Produktion | `CORE_DEBUG_LEVEL=0` | `-Os`, `-DNDEBUG` |

Partition: **Dual OTA** (`huge_app.csv`) – je Slot ca. 1,875 MB.

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
| [WEB_ADMIN.md](WEB_ADMIN.md) | HTTP-Routen, Auth, SSE |
| [HARDWARE.md](HARDWARE.md) | Board, Display, Pinbelegung |
| [OTA.md](OTA.md) | Firmware-Updates über GitHub |
| [CONFIGURATION.md](CONFIGURATION.md) | NVS-Namespaces, Defaults |
| [DISPLAY.md](DISPLAY.md) | Display-Task, Herz-Geometrie, Delta-Logik |
| [SECURITY.md](SECURITY.md) | Threat Model, Empfehlungen |
