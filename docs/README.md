# Setup

Chaya2MQTT is firmware for two [Waveshare ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) boards (SKU **34586**). Press BOOT on one; a red heart appears on the other via MQTT.

Product page: [README.md](../README.md). Board: [Waveshare 1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) (pins in `src/hw/pins_esp32_waveshare.h`). Tests: [TESTING.md](TESTING.md). Admin: [WEB_ADMIN.md](WEB_ADMIN.md).

## Build

Needs [PlatformIO](https://platformio.org/). If `pio` is missing: `~/.platformio/penv/bin/pio`.

```bash
pio run                   # release (default env)
pio run -e esp32s3        # debug
pio run -t upload
make upload               # debug flash, keep settings
make upload-erase         # erase, then debug flash
make monitor              # 115200
```

Or flash in Chrome/Edge: [web flasher](https://eyjunge1.github.io/chaya2mqtt/). Erase is on by default. Local flasher: [flasher/README.md](../flasher/README.md).

| Env | Use |
|-----|-----|
| `esp32s3-release` | Production (`-Os`, no logs) |
| `esp32s3` | Dev (`CORE_DEBUG_LEVEL=4`) |

8 MB flash, dual OTA (`partitions_chaya_8mb.csv`).

## First setup

1. Power on (on battery: press **PWR**; firmware holds GPIO17 HIGH).
2. No Wi‑Fi yet → SoftAP **`Chaya2MQTT`**. Scan the WIFI QR on the panel, or open `http://chaya2mqtt.local` / `http://4.3.2.1`.
3. Enter Wi‑Fi (AP mode tests the STA link before saving).
4. On **MQTT**: broker host, port **8883** (TLS default), optional user/password, partner ID (6 hex).
5. After STA: admin is `http://chaya2mqtt-<deviceId>.local`.

Broker must use a public CA (Let's Encrypt, …). Two devices can set up in parallel; same SSID, isolated APs — scan the QR on the intended display.

## Pairing

Same firmware version. Same broker. Wi‑Fi may differ.

1. Open `/mqtt` on both.
2. Put each device's ID as the other's **partner ID**. Unpair clears the partner only.
3. Topics (not editable): pub `chaya2mqtt/<own>`, sub `chaya2mqtt/<partner>`.

No partner → broker may still connect; no subscribe, no heart send, waiting title on the panel.

Payload is a decimal absolute counter, QoS 1, retained. Display shows deltas (`raw − baseline`, max `999+`).

## Factory reset

**Settings → Device → Factory reset** wipes NVS `wifi`, `mqtt`, `cfg`, `chaya` and reboots into SoftAP. No hardware reset gesture — if the UI is unreachable, erase and reflash USB.

## NVS

| NS | What |
|----|------|
| `wifi` | STA + SoftAP PSK |
| `mqtt` | Broker + partner (topics derived in RAM) |
| `cfg` | Device ID, UI, LED, audio, display view, OTA day/channel |
| `chaya` | Absolute counters + baselines |

Device ID is 6 hex in `cfg/device_id`. Wipe/erase → new ID and new topics.

## OTA

GitHub Releases, HTTPS + SHA-512 sidecar, Arduino `HTTPUpdate` into the next OTA slot. USB/first install uses `firmware.factory.bin` (bootloader + partitions + app). OTA uses **`firmware.bin` only**.

Daily check in STA (`stable` / `beta`). Install is manual. After OTA, image stays pending until ~30 s stable STA. Brick: web flasher or `make upload-erase`.

## Layout

```
src/          firmware (tasks in network/, async/, display/, ota/, button/, audio/)
frontend/     Svelte admin SPA — [frontend/README.md](../frontend/README.md)
flasher/      browser installer — [flasher/README.md](../flasher/README.md)
docs/         this folder; REST [api/openapi.yaml](api/openapi.yaml), SSE [api/asyncapi.yaml](api/asyncapi.yaml)
```

License: [GPL-3.0-only](../LICENSE).
