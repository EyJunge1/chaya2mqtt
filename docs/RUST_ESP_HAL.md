# Rust / esp-hal rewrite

Branch: `feat/rust-esp-hal`.

## Status

| Piece | State |
|-------|--------|
| Pure logic (`chaya-pure`) | Done |
| App modules (`chaya-fw` lib) | Done |
| **Device ELF** | **Builds** (~1.9 MB) |
| SoftAP (WPA2 PSK) + DHCP + captive DNS | Live |
| STA + DHCPv4 | Live |
| FlashKv (shared flash) | Live |
| EPD SoftAP/heart + TX/RX/battery + live redraw | Live |
| Embedded SPA + gzip Vite assets + OpenAPI + SSE keepalive | Live |
| MQTT TCP + TLS 1.3 (host DER pin) + RX beep/persist | Live |
| Button / LED / soft-off latch cut / battery ADC | Live |
| ES8311 + PA + I2S DMA TX/RX tones | Live |
| GitHub OTA (stable/beta) + SHA-512 flash + reboot | Live |
| Settings (`otaChannel`) + factory-reset + reboot | Live |
| Full CA/webpki TLS verify | Partial (hostname-in-leaf DER) |
| SoftAP scannable WIFI-QR on EPD | Partial (SSID/IP text) |
| Production flash | Still C++ until full parity |

## Build

```bash
cd rust/chaya-fw && cargo +esp build --features device --release
cd rust && cargo test -p chaya-pure -p chaya-fw
```

## HTTP (device)

- `POST /api/ota/check` · `POST /api/ota/install` · `GET /api/ota/status`
- `GET|POST /api/settings` (`otaChannel=stable|beta`)
- `POST /api/reboot` · `POST /api/factory-reset`
- SoftAP: `POST /api/wifi` speichert + rebootet in STA
- `GET /api/events` SSE mit Keepalive
