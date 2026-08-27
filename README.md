# Chaya2MQTT

Firmware for **Chaya2MQTT**: two paired [Waveshare ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G) (SKU 34586) hearts exchange counter values through an MQTT broker.

## Documentation

| File | Contents |
|-------|--------|
| [docs/README.md](docs/README.md) | Project overview, quick start, setup, pairing |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | FreeRTOS tasks, queues, mutexes, data flows |
| [docs/MODULES.md](docs/MODULES.md) | Code reference for all modules |
| [docs/MQTT.md](docs/MQTT.md) | MQTT protocol, topics, TLS, pairing |
| [docs/WEB_ADMIN.md](docs/WEB_ADMIN.md) | HTTP routes, CSRF, SSE |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Waveshare 1.54G (SKU 34586): pins, battery, buttons |
| [docs/OTA.md](docs/OTA.md) | Firmware updates through GitHub |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | NVS namespaces, defaults, factory reset |
| [docs/DISPLAY.md](docs/DISPLAY.md) | Display task, heart geometry, delta logic |
| [flasher/README.md](flasher/README.md) | Browser USB flasher (local preview; GitHub Pages later) |

## Quick start

```bash
pio run -e esp32s3-release -t upload   # Release build (recommended for deployed devices)
pio device monitor                       # Serial monitor (115200 baud)
```

For development with debug logs: `pio run -e esp32s3 -t upload`

Alternatively: `make upload` / `make monitor`. `make upload` erases the complete flash,
including all saved settings, before flashing.

A browser USB flasher (Chrome / Edge, no PlatformIO) lives under `flasher/`.
Preview it locally via [flasher/README.md](flasher/README.md). GitHub Pages
hosting for that installer comes later.

## Security

Wi-Fi and MQTT credentials are stored as plaintext in NVS, the web interface uses HTTP without a login, and OTA firmware is not cryptographically signed. The device is intended for use on a trusted home network.

## Contributing

Guidance for issues and pull requests is available in [CONTRIBUTING.md](CONTRIBUTING.md).

## License

This project is licensed under the [GNU General Public License v3.0 only](LICENSE).

You may use, share, modify, and distribute the code, including commercially. If you
distribute modified versions or binaries, you must provide the corresponding source code
under the same license. See [LICENSE](LICENSE) for details.

Third-party components remain under their respective licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
