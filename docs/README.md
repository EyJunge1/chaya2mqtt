# Chaya2MQTT – send a little heart

Chaya2MQTT turns two small ESP32-S3 e-paper displays into a private connection:
press the button on yours and a red heart appears on the other. It could be across
the room or across the world—as long as both hearts can reach the same MQTT broker.

Each display remembers how many hearts were sent and received. If one heart goes
offline for a while, it catches up automatically when it reconnects. The technical
details live below; the idea is simply a quiet little *thinking of you*. ❤️

## Features

| Area | Description |
|------|-------------|
| **E-Ink display** | Red heart with RX/TX counters (delta display), Waveshare 1.54G, 200×200, black/white/red/yellow |
| **MQTT sync** | Publish on button press or via the web UI; subscribe sets the counter to the received value |
| **TLS** | Broker connection via `mqtts://` with the **Mozilla CA bundle** (port **8883**) |
| **Web admin** | Captive portal in AP mode, then `http://chaya2mqtt-<deviceId>.local` |
| **Pairing** | Random device ID in NVS → automatic topics `chaya2mqtt/<id>` |
| **Button** | BOOT: press → send via MQTT |
| **OTA** | Automatic daily GitHub release check + manual trigger |

## Target hardware

Only [Waveshare ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) SKU **34586**. Pins, battery, buttons: [HARDWARE.md](HARDWARE.md).

Tests and quality gates: [TESTING.md](TESTING.md)

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE)
- MQTT broker with TLS and a valid server certificate from a **public CA** (Let's Encrypt, DigiCert, etc.)
- Waveshare ESP32-S3-ePaper-1.54G SKU 34586 (USB-C and/or included LiPo)

## Quick start

**Browser:** Open the hosted [web flasher](https://eyjunge1.github.io/chaya2mqtt/)
in Chrome, Edge, or another Chromium browser and connect the device over USB-C.
For local flasher development, see [flasher/README.md](../flasher/README.md).

```bash
# In the project directory
pio run                           # Build (default env: esp32s3-release)
pio run -e esp32s3                # Development build (CORE_DEBUG_LEVEL=4)
pio run -t upload                 # Flash (default: esp32s3-release)
pio device monitor                # Serial monitor (115200 baud)
```

Alternatively, use the Makefile (upload targets default to debug):

```bash
make upload                   # Build and flash debug; keep saved settings
make upload-erase             # Erase settings, build, and flash debug
make monitor                  # Serial monitor
```

If `pio: command not found` appears, PlatformIO is located at `~/.platformio/penv/bin/pio`. Either set `export PATH="$HOME/.platformio/penv/bin:$PATH"` or use `make`.

## Initial setup (WiFi & MQTT)

1. Power the device or restart it after flashing.
2. If no WiFi configuration is stored, the ESP32-S3 opens the WPA2/WPA3 **`Chaya2MQTT`** access point with an individual alphanumeric SoftAP password. The E-Ink display shows the WIFI QR (join by scanning only). On battery, press **PWR** to start (firmware must hold GPIO17 HIGH).
3. Scan the WIFI QR on the display with your phone camera (or join `Chaya2MQTT` manually using its displayed PIN) and open the captive portal / browser (`http://chaya2mqtt.local` or `http://4.3.2.1`).
4. Enter the **WiFi** SSID and password. After switching to STA mode, configure MQTT under **MQTT**:
   - **MQTT server** (hostname or IP) — required
   - **MQTT port** (default: **8883**) — required
   - **MQTT username / password** (optional; leave empty for anonymous brokers)
   - **Partner ID** of the other device (required for the heart display; 6 hex characters). Use Unpair to clear.
5. In AP mode, the WiFi connection is **tested** before the credentials are saved.
6. After a successful connection, the admin UI is available at **`http://chaya2mqtt-<deviceId>.local`**, for example `http://chaya2mqtt-a1b2c3.local`. The address and ID are shown on the dashboard.

Two devices can be set up in parallel. Both APs use the same SSID, but they are isolated networks with individual PINs; scanning the QR on the intended display joins the matching device. Once both devices share the normal LAN, their ID-suffixed hostnames keep web and mDNS access unambiguous. If static addressing is used, assign a different IP to every device.

## Pairing two devices

Flash both devices with the **same firmware version**. Broker credentials must be identical (WiFi can differ).

1. Open the **MQTT** page (`/mqtt`) on both devices.
2. Enter the same broker on both devices.
3. Note each device's own **device ID** (6 hexadecimal characters from the MAC), save it as the **partner ID** on the other device, and vice versa.
4. Topics are set automatically:
   - **Publish topic:** `chaya2mqtt/<own_id>` (e.g. `chaya2mqtt/a1b2c3`)
   - **Subscribe topic:** `chaya2mqtt/<partner_id>` (e.g. `chaya2mqtt/f5e6d7`)

This allows **multiple pairs** to use the same broker without topic collisions. Without a partner, the device can still connect to the broker but does not subscribe, keeps the waiting title (no heart with counters), and rejects heart send until a partner is set.

Details: [MQTT.md](MQTT.md)

## Factory reset

Use **Settings → Device → Factory reset** in the web admin. This deletes all NVS
namespaces (`wifi`, `mqtt`, `cfg`, `chaya`) and restarts into the **`Chaya2MQTT`**
setup SoftAP. There is intentionally no physical factory-reset gesture; if the web
admin is unreachable, erase and reflash the device over USB.

## Project structure

```
chaya2mqtt/
├── README.md                 # Brief introduction
├── platformio.ini            # Build configuration
├── partitions_chaya_8mb.csv  # Dual-OTA table (8 MB flash; ~3.75 MB per slot)
├── Makefile                  # PlatformIO wrapper
├── docs/                     # This documentation
├── flasher/                  # Svelte/Vite/Tailwind ESP Web Tools app (GitHub Pages)
├── frontend/                 # Svelte 5 SPA (Vite, Tailwind, Lucide) + mock device
└── src/
    ├── main.cpp              # Bootstrap, task startup
    ├── constants.h           # Device-wide identity, NTP, syntax validation
    ├── async/                # Queues, mutexes, app task
    ├── config/               # app_config, nvs_utils, version.h
    ├── diag/                 # Stack monitor, task WDT
    ├── display/              # EPD (GxEPD2) + drawing + display task
    ├── heart/                # Counters, baselines, NVS
    ├── hw/                   # Button, battery ADC, SD hold-off, 1.54G pins
    ├── mqtt/                 # Config + client/events/publish/reconnect
    ├── network/              # network_task (WiFi + MQTT loop)
    ├── ota/                  # GitHub check, flash installation, health gate
    ├── tls/                  # CA bundle (MQTT + OTA)
    ├── util/                 # Time helpers, logging, IP formatting
    ├── web/                  # Admin API + SPA serving (routes/, assets/, csrf)
    └── wifi/                 # WiFi, captive portal, recovery, connection test
```

## Build environments

| Environment | Purpose | Debug level | Optimization |
|-------------|---------|-------------|--------------|
| `esp32s3` | Development (ESP32-S3 1.54G) | `CORE_DEBUG_LEVEL=4` | Default |
| `esp32s3-release` | Production (default) | `CORE_DEBUG_LEVEL=0` | `-Os`, `-DNDEBUG` |

The 1.54G has **8 MB** flash. `partitions_chaya_8mb.csv` is a dual-OTA map (~3.75 MB per slot).

## Dependencies (PlatformIO)

| Library | Purpose |
|---------|---------|
| **GxEPD2** | E-paper driver (1.54G 4-color / `GxEPD2_4C`) |
| **Adafruit GFX / BusIO** | Graphics primitives for e-paper |
| **ESP-IDF MQTT** (`esp_mqtt_client`) | mqtt or mqtts (TLS selectable; default TLS) |
| **ESPAsyncWebServer** | HTTP admin + captive portal |

## Documentation

| File | Content |
|------|---------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | FreeRTOS tasks, queues, data flows |
| [MODULES.md](MODULES.md) | Code reference for all modules |
| [MQTT.md](MQTT.md) | Protocol, topics, TLS, pairing |
| [WEB_ADMIN.md](WEB_ADMIN.md) | HTTP routes, CSRF, SSE |
| [openapi.yaml](openapi.yaml) | REST API (OpenAPI 3.1) |
| [asyncapi.yaml](asyncapi.yaml) | SSE events (AsyncAPI 3) |
| [HARDWARE.md](HARDWARE.md) | The only board: 1.54G SKU 34586, pins, battery |
| [OTA.md](OTA.md) | Firmware updates via GitHub |
| [CONFIGURATION.md](CONFIGURATION.md) | NVS namespaces, defaults |
| [DISPLAY.md](DISPLAY.md) | Display task, Lucide icons, delta logic |

## License

[GNU General Public License v3.0 only](../LICENSE)—use, modification, and distribution
are permitted, including commercially. Distributed modified versions and binaries must
make the corresponding source code available under the same license.
