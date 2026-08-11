# Chaya2MQTT

Firmware for **Chaya2MQTT**: an ESP32 with an e-ink display and MQTT—two paired heart devices exchange counter values through an MQTT broker.

## Documentation

| File | Contents |
|-------|--------|
| [docs/README.md](docs/README.md) | Project overview, quick start, setup, pairing |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | FreeRTOS tasks, queues, mutexes, data flows |
| [docs/MODULES.md](docs/MODULES.md) | Code reference for all modules |
| [docs/MQTT.md](docs/MQTT.md) | MQTT protocol, topics, TLS, pairing |
| [docs/WEB_ADMIN.md](docs/WEB_ADMIN.md) | HTTP routes, authentication flow, SSE |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Board, display, pin assignment |
| [docs/OTA.md](docs/OTA.md) | Firmware updates through GitHub |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | NVS namespaces, defaults, factory reset |
| [docs/DISPLAY.md](docs/DISPLAY.md) | Display task, heart geometry, delta logic |

## Quick start

```bash
pio run -e esp32dev-release -t upload   # Release build (recommended for deployed devices)
pio device monitor                       # Serial monitor (115200 baud)
```

For development with debug logs: `pio run -e esp32dev -t upload`

Alternatively: `make upload` / `make monitor`

## Security

Wi-Fi and MQTT credentials are stored as plaintext in NVS, the web interface uses HTTP without a login, and OTA firmware is not cryptographically signed. The device is intended for use on a trusted home network.

## Contributing

Guidance for issues and pull requests is available in [CONTRIBUTING.md](CONTRIBUTING.md).

## License

This project is licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).

You may use, share, and modify the code—with attribution and under the same license. **Commercial use is not permitted.** See [LICENSE](LICENSE) for details.
