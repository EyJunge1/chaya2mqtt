# Rust / esp-hal rewrite

Branch: `feat/rust-esp-hal`.

## Status

| Piece | State |
|-------|--------|
| Pure logic (`chaya-pure`) | Done |
| App modules (`chaya-fw` lib) | Done |
| **Device ELF** | **Builds** (~1.6 MB+) |
| SoftAP + DHCP + captive DNS | Live |
| STA + DHCPv4 | Live |
| FlashKv | Live |
| EPD splash/heart | Live |
| Embedded SPA + OpenAPI stub + SSE | Live |
| MQTT TCP + TLS 1.3 (port ≥ 8883, NoVerify) | Live |
| Button / LED / soft-off / battery ADC | Live |
| ES8311 + PA beep | Live (I2S DMA samples TODO) |
| GitHub OTA tag check (TLS) | Live |
| SHA-512 verified flash install | **TODO** |
| Full Vite SPA assets in flash | **TODO** |
| Production flash | Still C++ until full parity |

## Build

```bash
cd rust/chaya-fw && cargo +esp build --features device --release
cd rust && cargo test -p chaya-pure -p chaya-fw
```

## Remaining

- OTA: stream `firmware.bin` + verify `firmware.sha512` via `esp-hal-ota`
- I2S DMA beep/samples into ES8311
- Cert verification for MQTT/GitHub TLS (replace NoVerify)
- Embed production Vite build
