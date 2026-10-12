# Setup

Chaya2MQTT is firmware for two [Waveshare ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) boards (SKU **34586**). Press BOOT on one; a red heart appears on the other via MQTT.

Product page: [README.md](../README.md). Board: [Waveshare 1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) (pins in `src/hw/mod.rs`). Tests: [TESTING.md](TESTING.md). Admin: [WEB_ADMIN.md](WEB_ADMIN.md).

## Build

**Production is Rust** (esp-hal). Needs [espup](https://esp-rs.github.io/book/installation/using-espup.html) + `espflash`.

```bash
make upload               # Rust release flash, keep NVS
make upload-erase         # erase chip, then flash
make monitor              # serial (espflash)
bash scripts/build_rust_release.sh   # → out/rust-release/{firmware,firmware.factory}.bin
```

Or flash in Chrome/Edge: [web flasher](https://eyjunge1.github.io/chaya2mqtt/). Erase is on by default. Local flasher: [flasher/README.md](../flasher/README.md).

The legacy C++/PlatformIO tree has been removed; firmware is Rust-only.

8 MB flash, dual OTA (`partitions.csv`).
## First setup

1. Power on (on battery: press **PWR**; firmware holds GPIO17 HIGH).
2. No Wi‑Fi yet → SoftAP **`Chaya2MQTT`**. Scan the WIFI QR on the panel, or open `http://chaya2mqtt.local` / `http://4.3.2.1`.
3. Enter Wi‑Fi (AP mode tests the STA link before saving).
4. On **MQTT**: broker host, port **8883** (TLS default), optional user/password, pairing ID (defaults to this device), partner ID (6 hex).
5. After STA: admin is `http://chaya2mqtt-<deviceId>.local`.

Default TLS is host-pin (leaf + SNI hostname check), not a full CA chain. Optional CA verify: build with `--features device,mbedtls` (see [RUST_ESP_HAL.md](RUST_ESP_HAL.md)). Two devices can set up in parallel; same SSID, isolated APs — scan the QR on the intended display.

## Pairing

Same firmware version. Same broker. Wi‑Fi may differ. Pairing is still one person ↔ one person; extra boards (home + office) **reuse the same two IDs**.

1. Open `/mqtt` on both first devices.
2. **Pairing ID** defaults to this device's ID. **Partner ID** is the other person's pairing ID. Unpair clears the partner only.
3. Topics (not editable): pub `chaya2mqtt/<pairingId or own>`, sub `chaya2mqtt/<partner>`. When paired, the device also subscribes to its own publish topic so a second board of the same person stays in sync.
4. **Office / extra own device:** enter the same pairing ID and partner ID as the home device of that person. The unique device ID (hostname, MQTT client, LWT `chaya2mqtt/<deviceId>/lwt`) stays on this board.

No partner → broker may still connect; no subscribe, no heart send, waiting title on the panel.

Payload is a decimal absolute counter, QoS 1, retained. Display shows deltas (`raw − baseline`, max `999+`). Own-topic updates only move TX forward (self-echo after send is ignored).

## Factory reset

**Settings → Device → Factory reset** wipes FlashKv namespaces `wifi`, `mqtt`, `cfg`, `chaya` and reboots into SoftAP. No hardware reset gesture — if the UI is unreachable, erase and reflash USB.

C++ ESP-IDF NVS blobs (`cfg_v1` / `cfg_v2`) are **not** imported — flash from the old firmware needs erase/reconfigure.

## FlashKv (namespaces)

`sequential-storage` map in the last 32 KiB of flash (on the 8 MB table that is the tail of the unused `spiffs` partition, `0x7F8000..0x800000`). Do not mount SPIFFS there.

The previous flat image (magic `0xC4A7A201`, last two sectors) is copied in once at boot. A power cut during that copy can drop settings; enter them again or factory-reset.

| NS | What |
|----|------|
| `wifi` | STA + SoftAP PSK |
| `mqtt` | Broker + partner + pairing (individual keys, not C++ blobs) |
| `cfg` | Device ID, UI, LED, audio, display view, OTA day/channel |
| `chaya` | Absolute counters + baselines |

Device ID is 6 hex in `cfg/device_id`. Wipe/erase → new ID and new topics.

## OTA

GitHub Releases, HTTPS + SHA-512 sidecar (`firmware.sha512`), verified flash via `esp-hal-ota` into the next OTA slot. USB/first install uses `firmware.factory.bin` (bootloader + partitions + app). OTA uses **`firmware.bin` only**.

Daily automatic GitHub check in STA once NTP is synced (`stable` / `beta`, skipped for `dev` builds; day stored in `cfg/upd_day`). Install stays manual via admin UI. After OTA, image stays pending until ~30 s stable STA. Brick: web flasher or `make upload-erase`.

## Layout

```
src/          firmware (esp-hal) — [RUST_ESP_HAL.md](RUST_ESP_HAL.md)
frontend/     Svelte admin SPA — [frontend/README.md](../frontend/README.md)
flasher/      browser installer — [flasher/README.md](../flasher/README.md)
docs/         this folder; REST [api/openapi.yaml](api/openapi.yaml), SSE [api/asyncapi.yaml](api/asyncapi.yaml)
```

License: [GPL-3.0-only](../LICENSE).
