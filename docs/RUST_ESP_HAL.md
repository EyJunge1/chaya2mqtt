# Rust / esp-hal rewrite

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
| Button / LED presets / soft-off latch / battery ADC | Live |
| ES8311 + I2S DMA TX/RX + quiet hours | Live |
| GitHub OTA (stable/beta) + SHA-512 flash + reboot | Live |
| Settings / MQTT JSON contracts (SPA field names) | Live |
| Full CA TLS via **mbedtls-rs** | Optional `--features device,mbedtls` (needs espup/cmake) |
| Production flash | Still C++ until on-device parity soak |

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

Thematic modules — host logic and device backends share a folder; device code is `#[cfg(feature = "device")]`.

```
app/        host boot orchestration (testable)
audio/      AudioConfig + ES8311 hw
board/      pin map / early GPIO helpers
boot/       SoftAP / STA bring-up (binary)
display/    DisplayState + EPD + QR (nah zusammen)
http/       edge-http server, routes/*, json DTOs+responses, SPA
identity/   device id, hearts, version
input/      button, LED, battery (+ ADC)
mqtt/       MqttConfig + session + proto + TLS pin
net/        NetworkService + NTP + mDNS
ota/        channel + GitHub client + flash stream
runtime/    Embassy tasks + shared signals (binary)
storage/    config ns, platform traits, FlashKv
wifi/       WlanConfig + ops + scan
web.rs      host WebAdminState stub
main.rs     thin entry (~200 lines)
```

## Build

```bash
# Toolchain: espup (provides cargo +esp and xtensa-esp-elf-gcc)
cd rust/chaya-fw && cargo +esp build --features device --release

# Optional hardware CA verify:
cd rust/chaya-fw && cargo +esp build --features device,mbedtls --release

cd rust && cargo test -p chaya-pure -p chaya-fw
```
