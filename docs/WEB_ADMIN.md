# Web-Administration

Die Weboberfläche ist eine **React-19-SPA** (Vite, Tailwind CSS, Lucide), die als gzip-komprimierte PROGMEM-Assets in der Firmware liegt. Der ESP32 liefert HTML/JS/CSS aus und spricht JSON + SSE; die UI läuft im Browser.

## Zugriff

| Modus | URL | Zugang |
|-------|-----|--------|
| AP (Setup) | Captive Portal / `http://4.3.2.1` | Offen im AP-Netz |
| STA | `http://chaya2mqtt.local` (mDNS) | Offen im lokalen Netz (kein Login) |

Hostname: `chaya2mqtt` (Konstante `kDeviceHostname`).

## Lokale Entwicklung (ohne Flashen)

```bash
cd frontend
npm ci
npm run dev
```

Der Vite-Devserver startet einen **virtuellen Chaya2MQTT** unter `frontend/mock/` mit denselben `/api/*`- und `/events`-Verträgen. Szenarien (STA, AP, Offline) lassen sich über die Simulator-Leiste umschalten.

```bash
make frontend-test   # Vitest
make frontend        # Production-Build + PROGMEM-Header generieren
```

## SPA-Routen (UI)

Alle UI-Pfade liefern dieselbe `index.html` (Client-Router):

`/`, `/wifi`, `/wifi-testing`, `/mqtt`, `/pairing`, `/settings`, `/update`

Statische Assets:

| Route | Inhalt |
|-------|--------|
| `/assets/app.js` | gzip JS-Bundle |
| `/assets/app.css` | gzip CSS |

## JSON-API

Mutationen erwarten `application/x-www-form-urlencoded` inkl. `csrf_token`.

| Route | Methode | Schutz | Beschreibung |
|-------|---------|--------|--------------|
| `/api/csrf` | GET | Host | `{token}` |
| `/api/device` | GET | Host | Modus, Version, Device-ID |
| `/api/chaya` | GET | Host + STA | `{rx,tx,connected}` |
| `/api/chaya/send` | POST | CSRF + STA | Herz senden (queued) |
| `/api/wifi/status` | GET | Host | Link-Status |
| `/api/wifi/scan` | GET | Host | AP-Liste oder **202** |
| `/api/wifi/connect` | POST | CSRF | Credentials / AP-Test |
| `/api/wifi/connect-status` | GET | AP | Test-State |
| `/api/wifi/connect-commit` | POST | AP + CSRF | Speichern + Reboot |
| `/api/wifi/connect-abort` | POST | AP + CSRF | Test abbrechen |
| `/api/mqtt` | GET/POST | Host/CSRF + STA | Broker-Config (Passwort nie in GET) |
| `/api/mqtt/status` | GET | Host + STA | `{connected}` |
| `/api/pairing` | GET/POST | Host/CSRF + STA | Device-/Partner-ID |
| `/api/settings` | GET/POST | Host/CSRF + STA | Reset-Tage |
| `/api/reboot` | POST | CSRF + STA | Neustart (deferred) |
| `/api/update/check` | POST | CSRF + STA | GitHub-OTA-Check |

## Server-Sent Events

| Route | Events |
|-------|--------|
| `/events` | `chaya`, `wifi`, `mqtt` (wie bisher) |

Max. **6** SSE-Clients. Tick alle 500 ms im App-Task.

## CSRF / Sicherheit

- Kein Web-Login; Admin-UI ist für Teilnehmer des lokalen Netzes erreichbar
- CSRF-Token über `/api/csrf`, in jedem POST als `csrf_token`
- Host-/Origin-Allowlist; CSP ohne Inline-Script/Style (`script-src 'self'; style-src 'self'`)

## Build-Integration

1. `frontend/` → `npm run build` → `frontend/dist/`
2. `tools/embed_web_assets.py` → `src/web/assets/spa_{html,js,css}.h`
3. PlatformIO `pre:scripts/pio_pre_frontend.py` führt das vor jedem Firmware-Build aus
4. Soft-Limit: komprimierte SPA ≤ 350 KiB; OTA-Slot 1,875 MB

## Deferred Work

Unverändert: MQTT-/Settings-Apply und Reboot laufen deferred im App-Task. WiFi-Credential-Schreiben kann im Request-Pfad NVS schreiben.

## Weitere Dokumentation

- Architektur: [ARCHITECTURE.md](ARCHITECTURE.md)
- Konfiguration: [CONFIGURATION.md](CONFIGURATION.md)
- Sicherheit: [SECURITY.md](SECURITY.md)
