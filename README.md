# Chaya2MQTT

Firmware für **Chaya2MQTT**: ESP32 mit E-Ink-Display und MQTT – zwei gekoppelte Herz-Geräte tauschen Zählerstände über einen MQTT-Broker aus.

## Dokumentation

| Datei | Inhalt |
|-------|--------|
| [docs/README.md](docs/README.md) | Projektübersicht, Schnellstart, Einrichtung, Pairing |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | FreeRTOS-Tasks, Queues, Mutexe, Datenflüsse |
| [docs/MODULES.md](docs/MODULES.md) | Code-Referenz aller Module |
| [docs/MQTT.md](docs/MQTT.md) | MQTT-Protokoll, Topics, TLS, Pairing |
| [docs/WEB_ADMIN.md](docs/WEB_ADMIN.md) | HTTP-Routen, Auth-Flow, SSE |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Board, Display, Pinbelegung |
| [docs/OTA.md](docs/OTA.md) | Firmware-Updates über GitHub |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | NVS-Namespaces, Defaults, Factory Reset |
| [docs/DISPLAY.md](docs/DISPLAY.md) | Display-Task, Herz-Geometrie, Delta-Logik |
| [docs/SECURITY.md](docs/SECURITY.md) | Threat Model, Empfehlungen |

## Schnellstart

```bash
pio run -e esp32dev-release -t upload   # Release-Build (empfohlen für Feldgeräte)
pio device monitor                       # Serial-Monitor (115200 Baud)
```

Für Entwicklung mit Debug-Logs: `pio run -e esp32dev -t upload`

Oder: `make upload` / `make monitor`

## Sicherheit

Siehe [docs/SECURITY.md](docs/SECURITY.md) – NVS-Klartext, HTTP ohne TLS, OTA ohne Code-Signatur.
