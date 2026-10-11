# Rust / esp-hal (production)

Branch: `feat/rust-esp-hal`.

## Status

| Piece | State |
|-------|--------|
| Pure logic (`chaya-pure`) | Done |
| App modules (`chaya-fw` lib) | Done |
| **Device ELF** | **Builds** (needs espup toolchain) |
| SoftAP (WPA2 PSK, **APSTA**) + DHCP + captive + WIFI QR on EPD | Live |
| STA + DHCPv4 + SNTP + **edge-mdns** (`chaya2mqtt-<id>.local`) | Live |
| FlashKv (shared flash) | Live |
| EPD SoftAP QR / heart + TX/RX/battery + live redraw | Live |
| **edge-http** admin server (SPA OpenAPI paths + SSE `/events`) | Live |
| WiFi scan + connect-test (AP) / save (STA) | Live |
| Embedded SPA + gzip Vite assets + OpenAPI | Live (HTML/JSON; binary assets via SPA finder) |
| MQTT TCP + TLS 1.3 (host DER pin; optional `mbedtls` feature) | Live |
| Button / LED presets / soft-off + deep sleep EXT1 / battery ADC | Live |
| ES8311 + I2S DMA TX beeps + quiet hours | Live |
| GitHub OTA (stable/beta) + SHA-512 flash + reboot + health mark-valid | Live |
| Settings / MQTT JSON (led/lang/theme/resetDays + audio en/vol/quiet) | Live |
| MQTT QoS1 + RETAIN + PUBACK + LWT + own-topic echo filter | Live |
| WiFi STA recovery (reassoc / controlled restart) | Live |
| Gzip SPA blob assets (`Content-Encoding: gzip`) | Live |
| Full CA TLS via **mbedtls-rs** | Optional `--features device,mbedtls` (needs espup/cmake) |
| **Production flash** | **Rust** (`make upload` / `scripts/build_rust_release.sh`) — C++ via `make upload-cpp` |

## Dependencies (device)

| Concern | Crate |
|---------|--------|
| HTTP / SSE | `edge-http` |
| Captive DNS / DHCP | `edge-captive`, `edge-dhcp` |
| mDNS | `edge-mdns` |
| WiFi | `esp-radio` (APSTA for SoftAP scan) |
| OTA flash | `esp-hal-ota` |
| SNTP | `sntpc` (+ thin Embassy UDP adapter; `sntpc-net-embassy` IPv6 mismatch) |
| JSON request DTOs | `serde` + `serde-json-core` |
| Audio codec | `es8311` |
| TLS default | `embedded-tls` + host-pin verifier |
| TLS CA (optional) | `mbedtls-rs` (`esp-rs/esp-mbedtls`) |

**Skipped:** `reqwless` (TLS/io-async version clash with current `embedded-tls`). **Not used (yet):** `rust-mqtt` — MQTT 3.1.1 (`v3`) feature is still an empty stub; hearts stay on the thin custom 3.1.1 client. `webpki`/`ring` is unsuitable for Xtensa without a full C toolchain.

## Source layout (`chaya-fw/src`)

Aligned with C++ `src/` folder names. Host logic and device backends share a theme folder; device code is `#[cfg(feature = "device")]`.

```
app/          host setup orchestration (testable)
async_rt/     Embassy tasks + signals  (C++: async/ — `async` is a Rust keyword)
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
tls/          host-pin TLS verifier
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

# Optional hardware CA verify:
cd rust/chaya-fw && cargo +esp build --features device,mbedtls --release

cd rust && cargo test -p chaya-pure -p chaya-fw

# Legacy C++ PlatformIO flash:
make upload-cpp
```

Partition table: root `partitions_chaya_8mb.csv` (synced into `rust/chaya-fw/partitions.csv` on release build).