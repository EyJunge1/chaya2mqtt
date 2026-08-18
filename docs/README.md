# Chaya2MQTT – E-Ink Heart with MQTT

Two ESP32-S3 devices, each with a **4-color e-paper display**, show a **red heart** with **counters**. When the button on **device A** is pressed, it publishes the next sent counter as a **retained MQTT message** to its **publish topic**; **device B** receives it on its **subscribe topic**, sets its counter to the received value, and updates the display. The same applies in reverse—each device has separate publish and subscribe topics, so that only the counter on the **other** device changes.

Thanks to **retained messages**, a device automatically retrieves the current counter after an offline period as soon as it reconnects.

## Features

| Area | Description |
|------|-------------|
| **E-Ink display** | Red heart with RX/TX counters (delta display), Waveshare 1.54G, 200×200, black/white/red/yellow |
| **MQTT sync** | Publish on button press or via the web UI; subscribe sets the counter to the received value |
| **TLS** | Broker connection via `mqtts://` with the **Mozilla CA bundle** (port **8883**) |
| **Web admin** | Captive portal in AP mode, then `http://chaya2mqtt.local` |
| **Pairing** | Device ID derived from MAC → automatic topics `chaya2mqtt/<id>` |
| **Button** | BOOT: short press → send via MQTT; hold for 10 s → factory reset |
| **OTA** | Automatic daily GitHub release check + manual trigger |

## Target hardware

Only [Waveshare ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) SKU **34586**. Pins, battery, buttons: [HARDWARE.md](HARDWARE.md).

Tests and quality gates: [TESTING.md](TESTING.md)

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE)
- MQTT broker with TLS and a valid server certificate from a **public CA** (Let's Encrypt, DigiCert, etc.)
- Waveshare ESP32-S3-ePaper-1.54G SKU 34586 (USB-C and/or included LiPo)

## Quick start

```bash
# In the project directory
pio run                        # Build (debug: CORE_DEBUG_LEVEL=3)
pio run -e esp32dev-release    # Production without serial debug
pio run -t upload              # Flash
pio device monitor             # Serial monitor (115200 baud)
```

Alternatively, use the Makefile:

```bash
make build                         # Build debug version
make upload ENV=esp32dev-release   # Build and flash release
make monitor                       # Serial monitor
```

If `pio: command not found` appears, PlatformIO is located at `~/.platformio/penv/bin/pio`. Either set `export PATH="$HOME/.platformio/penv/bin:$PATH"` or use `make`.

## Initial setup (WiFi & MQTT)

1. Power the device or restart it after flashing.
2. If no WiFi configuration is stored, the ESP32-S3 opens the **`Chaya2MQTT`** access point without authentication. The E-Ink display shows the SSID and setup URL. On battery, press **PWR** to start (firmware must hold GPIO17 HIGH).
3. Connect with a phone or PC and open the captive portal or a browser (typically `http://4.3.2.1`).
4. Enter the **WiFi** SSID and password. After switching to STA mode, configure MQTT under **MQTT**:
   - **MQTT server** (hostname or IP)
   - **MQTT port** (default: **8883**)
   - **MQTT username / password** (optional)
   - **Partner ID** of the other device (6 hexadecimal characters)
5. In AP mode, the WiFi connection is **tested** before the credentials are saved.
6. After a successful connection, the admin UI is available at **`http://chaya2mqtt.local`**.

## Pairing two devices

Flash both devices with the **same firmware version**. Broker credentials must be identical (WiFi can differ).

1. Open the **MQTT** page (`/mqtt`) on both devices.
2. Enter the same broker on both devices.
3. Note each device's own **device ID** (6 hexadecimal characters from the MAC), save it as the **partner ID** on the other device, and vice versa.
4. Topics are set automatically:
   - **Publish topic:** `chaya2mqtt/<eigene_id>` (e.g. `chaya2mqtt/a1b2c3`)
   - **Subscribe topic:** `chaya2mqtt/<partner_id>` (e.g. `chaya2mqtt/f5e6d7`)

This allows **multiple pairs** to use the same broker without topic collisions. Without a partner, the device remains connected to the broker but does not subscribe to a device topic.

Details: [MQTT.md](MQTT.md)

## Factory reset

Hold **BOOT** for **at least 10 seconds** → all NVS namespaces (`wifi`, `mqtt`, `cfg`, `chaya`) are deleted and the device restarts. The open **`Chaya2MQTT`** SoftAP is then available again. Do not hold BOOT while resetting if you only want to flash.

## Project structure

```
chaya2mqtt/
├── README.md                 # Brief introduction
├── platformio.ini            # Build configuration
├── huge_app.csv              # Dual-OTA table (chip is 8 MB; table still 4 MB-sized until S3 env)
├── Makefile                  # pio-Wrapper
├── docs/                     # This documentation
└── src/
    ├── main.cpp              # Bootstrap, task startup
    ├── constants.h           # Device-wide identity, NTP, syntax validation
    ├── async/                # Queues, mutexes, app task
    ├── config/               # app_config, nvs_utils, version.h
    ├── diag/                 # Stack monitor, task WDT
    ├── display/              # EPD (GxEPD2) + drawing + display task
    ├── heart/                # Counters, baselines, NVS
    ├── hw/                   # Button and 1.54G pins
    ├── mqtt/                 # Config + client/events/publish/reconnect
    ├── network/              # network_task (WiFi + MQTT loop)
    ├── ota/                  # GitHub check, flash installation, health gate
    ├── tls/                  # CA bundle (MQTT + OTA)
    ├── util/                 # Time helpers, logging, IP formatting
    ├── web/                  # Admin API + SPA serving (routes/, assets/, csrf)
    └── wifi/                 # WiFi, captive portal, recovery, connection test
frontend/                     # React 19 SPA (Vite, Tailwind, Lucide) + mock device
```

## Build environments

| Environment | Purpose | Debug level | Optimization |
|-------------|---------|-------------|--------------|
| `esp32dev` | Development (legacy `esp32dev` env until the S3 port) | `CORE_DEBUG_LEVEL=3` | Default |
| `esp32dev-release` | Production (default; same caveat) | `CORE_DEBUG_LEVEL=0` | `-Os`, `-DNDEBUG` |

The 1.54G has **8 MB** flash. `huge_app.csv` is still a **4 MB-sized** dual-OTA map (~1.875 MB per slot) until the S3 environment lands.

## Dependencies (PlatformIO)

| Library | Purpose |
|---------|---------|
| **GxEPD2** | E-paper driver (1.54G 4-color / `GxEPD2_4C`) |
| **Adafruit GFX / BusIO** | Graphics primitives for e-paper |
| **ESP-IDF MQTT** (`esp_mqtt_client`) | MQTT over TLS (not PubSubClient) |
| **ESPAsyncWebServer** | HTTP admin + captive portal |

## Documentation

| File | Content |
|------|---------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | FreeRTOS tasks, queues, data flows |
| [MODULES.md](MODULES.md) | Code reference for all modules |
| [MQTT.md](MQTT.md) | Protocol, topics, TLS, pairing |
| [WEB_ADMIN.md](WEB_ADMIN.md) | HTTP routes, CSRF, SSE |
| [openapi.yaml](openapi.yaml) | REST-API (OpenAPI 3.1) |
| [asyncapi.yaml](asyncapi.yaml) | SSE-Events (AsyncAPI 3) |
| [HARDWARE.md](HARDWARE.md) | The only board: 1.54G SKU 34586, pins, battery |
| [OTA.md](OTA.md) | Firmware updates via GitHub |
| [CONFIGURATION.md](CONFIGURATION.md) | NVS namespaces, defaults |
| [DISPLAY.md](DISPLAY.md) | Display task, heart geometry, delta logic |

## License

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)—adaptation and sharing with attribution are permitted; **commercial use is prohibited**. Full text: [LICENSE](../LICENSE).
