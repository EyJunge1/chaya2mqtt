# Web-Administration

Die Weboberfläche ist eine **React-19-SPA** (Vite, Tailwind CSS, Lucide), die als gzip-komprimierter Asset-Blob in der Firmware liegt. Der ESP32 liefert HTML/JS/CSS aus dem Manifest-Lookup und spricht JSON + SSE; die UI läuft im Browser.

## Zugriff

| Modus | URL | Zugang |
|-------|-----|--------|
| AP (Setup) | Captive Portal / `http://4.3.2.1` | Offener SoftAP `Chaya2MQTT` |
| STA | `http://chaya2mqtt.local` (mDNS) | Offen im lokalen Netz (kein Login) |

Captive-Portal-Probes (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, …) leiten im AP-Modus auf `http://4.3.2.1/wifi`.

Hostname: `chaya2mqtt` (Konstante `kDeviceHostname`).

## Lokale Entwicklung (ohne Flashen)

```bash
cd frontend
npm ci
npm run dev
```

Der Vite-Devserver startet einen **virtuellen Chaya2MQTT** unter `frontend/mock/` mit denselben `/api/*`- und `/events`-Verträgen. Die Simulator-Leiste (nur Dev) bleibt auch bei Boot-Loading und „Could not connect to the device“ sichtbar und steuert Presets sowie gezielte API-/SSE-Fehler.

### Simulator-Presets

| Gruppe | Szenarien |
|--------|-----------|
| Device | `sta-connected`, `boot-unreachable`, `boot-slow`, `sse-disconnected` |
| Network | `offline`, `sta-mqtt-offline`, `sta-mqtt-unconfigured`, `sta-mqtt-unpaired` |
| Wi‑Fi / Setup | `ap-setup`, `wifi-scan-empty`, `wifi-scan-fail`, `ap-test-idle`, `ap-test-testing`, `ap-test-ok`, `ap-test-failed` |
| Update | `update-available`, `update-checking`, `update-busy`, `update-verifying`, `update-rebooting`, `update-error` |

### Fault-Injection (API-/SSE-Fehler)

Über die Toolbar oder `POST /api/_mock/fault` (`fault=<key>&enabled=1`, `clear=1`) lassen sich Fehler reproduzieren, ohne den Gerätezustand umzuschreiben:

- Load errors: `device`, `mqtt`, `settings`, `update-status`, `sse`, …
- Action errors: `mqtt-save`, `settings-save`, `reboot`, `factory-reset`, `heart`, `wifi-scan`, `wifi-connect`, `wifi-commit`, `wifi-abort`, `update-check`, `update-install`, …

Weitere Dev-Endpoints: `POST /api/_mock/scenario`, `POST /api/_mock/reset`, `GET /api/_mock/state`.

```bash
make frontend-test   # Vitest
make frontend        # Production-Build + Asset-Blob/Manifest generieren
```

## SPA-Routen (UI)

Alle UI-Pfade liefern dieselbe `index.html` (Client-Router / SPA-Fallback):

`/`, `/wifi`, `/wifi-testing`, `/mqtt`, `/settings`, `/update`

Alte Links auf `/pairing` leitet die SPA nach `/mqtt` um.

Statische Assets kommen mit Content-Hash aus Vite und liegen unter `/assets/*` (Cache-Control: `immutable`). HTML bleibt `no-cache`.

## JSON-API

Mutationen erwarten `application/x-www-form-urlencoded` inkl. `csrf_token`.

| Route | Methode | Schutz | Beschreibung |
|-------|---------|--------|--------------|
| `/api/csrf` | GET | Host | `{token}` |
| `/api/device` | GET | Host | Modus, Version, Device-ID; im AP-Modus zusätzlich `apSsid` / `apIp` |
| `/api/chaya` | GET | Host + STA | `{rx,tx,connected}` |
| `/api/chaya/send` | POST | CSRF + STA | Herz senden (queued) |
| `/api/wifi/status` | GET | Host | Link-Status inkl. laufender IP/Gateway/Netmask/DNS |
| `/api/wifi/config` | GET | Host | Gespeicherte WLAN-Konfiguration (ohne Passwort) |
| `/api/wifi/scan` | GET | Host | AP-Liste oder **202** |
| `/api/wifi/connect` | POST | CSRF | Credentials + IP-Modus/DNS/NTP; AP-Test oder STA-Save+Reboot |
| `/api/wifi/connect-status` | GET | AP | Test-State |
| `/api/wifi/connect-commit` | POST | AP + CSRF | Speichern + Reboot |
| `/api/wifi/connect-abort` | POST | AP + CSRF | Test abbrechen |
| `/api/mqtt` | GET/POST | Host/CSRF + STA | Broker + Partner-ID (Topics abgeleitet; Passwort nie in GET) |
| `/api/mqtt/status` | GET | Host + STA | `{connected}` |
| `/api/settings` | GET/POST | Host/CSRF + STA | Reset-Tage, UI-Prefs, E-Ink Dark Mode (`displayDark` / `display_dark`) |
| `/api/reboot` | POST | CSRF + STA | Neustart (deferred) |
| `/api/factory-reset` | POST | CSRF + STA | Alle NVS-Daten löschen und im AP-Setup neu starten |
| `/api/update/status` | GET | Host + STA | OTA-Status (Phase, Kanal, Versionen, Fortschritt) |
| `/api/update/check` | POST | CSRF + STA | GitHub-OTA-Check (`channel=stable\|beta` optional) |
| `/api/update/install` | POST | CSRF + STA | Bestätigte Installation starten |

Maschinenlesbare Verträge:

- REST: [openapi.yaml](openapi.yaml) (OpenAPI 3.1)
- SSE: [asyncapi.yaml](asyncapi.yaml) (AsyncAPI 3)

## Server-Sent Events

| Route | Events |
|-------|--------|
| `/events` | `chaya`, `wifi`, `mqtt`, `ota` |

Max. **6** SSE-Clients. Tick alle 500 ms im App-Task.

## CSRF / Sicherheit

- Kein Web-Login; Admin-UI ist für Teilnehmer des lokalen Netzes erreichbar
- CSRF-Token über `/api/csrf`, in jedem POST als `csrf_token`
- Host-/Origin-Allowlist; CSP ohne Inline-Script/Style (`script-src 'self'; style-src 'self'`)

## Build-Integration

1. `frontend/` → `npm run build` → `frontend/dist/`
2. `scripts/embed_web_assets.py` packt alle Dist-Dateien gzip-komprimiert in:
   - `src/web/assets/web_ui.bin` (Blob)
   - `src/web/assets/web_ui_blob.S` (`.incbin` in Flash-Rodata)
   - `src/web/assets/web_ui_manifest.h` (Pfad, Offset, Länge, MIME, Cache-Klasse)
3. PlatformIO `pre:scripts/pio_pre_frontend.py` führt das vor jedem Firmware-Build aus
4. Soft-Limit: komprimierte SPA ≤ 350 KiB; OTA-Slot 1,875 MB
5. Generierte Blob-Artefakte sind gitignored und werden beim Build neu erzeugt

## Deferred Work

Unverändert: MQTT-/Settings-Apply und Reboot laufen deferred im App-Task. WiFi-Credential-Schreiben kann im Request-Pfad NVS schreiben.

## Weitere Dokumentation

- Architektur: [ARCHITECTURE.md](ARCHITECTURE.md)
- Konfiguration: [CONFIGURATION.md](CONFIGURATION.md)
- Sicherheit: [SECURITY.md](SECURITY.md)
