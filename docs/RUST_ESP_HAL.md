# Rust / esp-hal (production)

Production firmware is Rust-only (`src/` in the repo root). The legacy C++/PlatformIO tree has been removed.

## Status

| Piece | State |
|-------|--------|
| Host-testable core logic (`src/core`) | Done |
| App / device modules (`src/`) | Done |
| **Device ELF** | **Builds** (needs espup toolchain) |
| SoftAP (WPA2 PSK, **APSTA**) + DHCP + captive + WIFI QR on EPD | Live |
| STA + DHCPv4 + SNTP + **edge-mdns** (`chaya2mqtt-<id>.local`) | Live |
| FlashKv (shared flash) | Live |
| EPD SoftAP QR / heart + TX/RX/battery + live redraw | Live |
| **edge-http** admin server (SPA OpenAPI paths + SSE `/events`) | Live |
| WiFi scan + connect-test (AP) / save (STA) | Live |
| Embedded SPA + gzip Vite assets + OpenAPI | Live (HTML/JSON; binary assets via SPA finder) |
| MQTT TCP + TLS (default host-pin; optional `mbedtls` CA) | Live |
| Button / LED presets / soft-off + deep sleep EXT1 / battery ADC | Live |
| ES8311 + I2S DMA TX beeps + quiet hours | Live |
| GitHub OTA (stable/beta) + daily STA check + SHA-512 flash + reboot + health mark-valid | Live |
| Settings / MQTT JSON (led/lang/theme/resetDays + audio en/vol/quiet) | Live |
| MQTT QoS1 + RETAIN + PUBACK + LWT + own-topic echo filter | Live |
| WiFi STA recovery (reassoc / controlled restart) | Live |
| Gzip SPA blob assets (`Content-Encoding: gzip`) | Live |
| Full CA TLS via **mbedtls-rs** | Optional `--features device,mbedtls` (see TLS section) |
| **Production flash** | **Rust** (`make upload` / `scripts/build_rust_release.sh`) |
| FreeRTOS `diag/` (stack HWM / task WDT) | N/A — C++ used ESP-IDF TWDT/`uxTaskGetStackHighWaterMark`; embassy/esp-hal has no FreeRTOS task model |

## Dependencies (device)

| Concern | Crate |
|---------|--------|
| HTTP / SSE | `edge-http` |
| Captive DNS / DHCP | `edge-captive`, `edge-dhcp` |
| mDNS | `edge-mdns` |
| WiFi | `esp-radio` (APSTA for SoftAP scan) |
| OTA flash | `esp-hal-ota` |
| OTA HTTP headers | `httparse` (shared helpers in `ota/http_headers.rs`) |
| SNTP | `sntpc` (+ thin Embassy UDP adapter; `sntpc-net-embassy` IPv6 mismatch) |
| JSON request/response | `serde` + `serde-json-core` |
| MQTT 3.1.1 | `mqttrust` (`mqttv3`); heart / own-topic policy stays in `mqtt/session.rs` |
| Integer formatting | `itoa` |
| Audio codec | `es8311` |
| TLS default | `embedded-tls` + host-pin verifier (`tls/host_pin.rs`) |
| TLS CA (optional) | `mbedtls-rs` + `mozilla-root-ca` (Mozilla/`cacert.pem`, via `build.rs`) |

**Skipped:** `reqwless` (TLS/io-async version clash with current `embedded-tls`). **Not used:** `rust-mqtt` — MQTT 3.1.1 (`v3`) feature is still an empty stub. `webpki`/`ring` is unsuitable for Xtensa without a full C toolchain.

## Source layout (`src/`)

Host logic and device backends share a theme folder; device code is `#[cfg(feature = "device")]`.

```
app/          host setup orchestration (testable)
async_rt/     Embassy tasks + signals  (`async` is a Rust keyword)
audio/        AudioConfig + ES8311
battery/      BatteryService + ADC
boot/         SoftAP / STA bring-up (binary-only)
button/       debounce / soft-off
config/       NVS namespaces, version, FlashKv
display/      DisplayState + EPD + QR
heart/        RX/TX counters
hw/           pins / latch / SD hold
identity/     device id
led/          LED presets
mqtt/         MqttConfig + session + proto
network/      NetworkService + NTP + mDNS
ota/          channel + GitHub client + flash
tls/          `connect()` — host-pin **or** mbedtls CA (`--features mbedtls`)
util/         platform traits (Kv/GPIO/clock)
web/          edge-http, routes/*, SPA, admin state
wifi/         WlanConfig + ops + scan
main.rs       thin entry
```

## Build / flash (production)

```bash
# Toolchain: espup + espflash (cargo +esp, xtensa-esp-elf-gcc)
make upload            # build release + flash (keep NVS)
make upload-erase      # erase chip, then flash
make monitor           # serial

# Images only (OTA + factory) → out/rust-release/
bash scripts/build_rust_release.sh

# Optional CA TLS (cmake + `xtensa-esp-elf-gcc` on PATH; mbedtls `use-gcc`):
cargo +esp build --features device,mbedtls --release \
  --target xtensa-esp32s3-none-elf -Z build-std=core,alloc

cargo test
```

### TLS: default vs `mbedtls`

| Build | Backend | Verification |
|-------|---------|--------------|
| `--features device` (default) | `embedded-tls` | Leaf present + SNI hostname bytes in DER (no CA chain) |
| `--features device,mbedtls` | `mbedtls-rs` | CA chain + hostname (`AuthMode::Required`) |

MQTT / OTA call a thin `crate::tls::connect(...)` wrapper; buffers are used only by the embedded-tls path.

**mbedtls gaps / notes:**

- No esp32s3 “system” CRT bundle in mbedtls-rs — trust store is the `mozilla-root-ca` crate (~190 KiB Mozilla/`cacert.pem`, refreshed by Cargo).
- Certificate date window (`notBefore`/`notAfter`) is **not** checked unless `hook-wall-clock` + a wall clock are enabled (mbedtls-rs default).
- One global `Tls` singleton (mbedtls-rs); MQTT and OTA share it.
- Needs extra heap for the parsed CA chain vs the default path (esp-mbedtls examples use ~140 KiB heap).
- Pulls in `tinyrlibc` for mbedtls C `calloc`/`free`/mem* symbols.
- Build uses mbedtls `use-gcc` (`xtensa-esp-elf-gcc` on `PATH`); Apple clang cannot target `esp32s3`.
- CA PEM is re-parsed per `connect` (`Certificate` is `!Send`, so no shared cache).

Partition table: root `partitions_chaya_8mb.csv` (synced into `partitions.csv` on release build).