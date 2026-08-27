# Chaya2MQTT

Two [Waveshare ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G)
(SKU **34586**) devices show a red heart with counters. Pressing the button on one
device publishes the next sent count over MQTT; the partner receives it and updates
its display. Pairing uses device IDs derived from the MAC address.

## Flash

**Browser (recommended):** open the
[web flasher](https://eyjunge1.github.io/chaya2mqtt/) in Chrome or Edge, connect
USB-C (data cable), and install. Prefer erase on a first or recovery flash.

**PlatformIO:**

```bash
pio run -e esp32s3-release -t upload
```

Use `make upload` to keep saved settings, or `make upload-erase` for a factory-style
flash that erases them.

After flash with no Wi-Fi credentials, the device opens SoftAP `Chaya2MQTT`. Scan
the WIFI QR on the display and finish setup in the captive portal.

## Docs & license

Full setup, pairing, MQTT, and hardware notes: [docs/README.md](docs/README.md).

[GNU GPL v3.0 only](LICENSE) · [Contributing](CONTRIBUTING.md) ·
[Third-party notices](THIRD_PARTY_NOTICES.md)
