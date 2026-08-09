# Web-Administration

Die Weboberfläche ist eine **React-19-SPA** (Vite, Tailwind CSS, Lucide), die als gzip-komprimierte PROGMEM-Assets in der Firmware liegt. Der ESP32 liefert HTML/JS/CSS aus und spricht JSON + SSE; die UI läuft im Browser.

## Zugriff

| Modus | URL | Auth |
|-------|-----|------|
| AP (Setup) | Captive Portal / `http://4.3.2.1` | Offen (WLAN-Routen) |
| STA | `http://chaya2mqtt.local` (mDNS) | Optional (Settings) |

Hostname: `chaya2mqtt` (Konstante `kDeviceHostname`).

## Lokale Entwicklung (ohne Flashen)

```bash
cd frontend
npm ci
npm run dev
```

Der Vite-Devserver startet einen **virtuellen Chaya2MQTT** unter `frontend/mock/` mit denselben `/api/*`- und `/events`-Verträgen. Szenarien (STA, Auth, AP, Offline) lassen sich über die Simulator-Leiste umschalten.

```bash
make frontend-test   # Vitest
make frontend        # Production-Build + PROGMEM-Header generieren
```

## SPA-Routen (UI)

Alle UI-Pfade liefern dieselbe `index.html` (Client-Router):

`/`, `/wifi`, `/wifi-testing`, `/mqtt`, `/pairing`, `/settings`, `/update`, `/auth`

Statische Assets:

| Route | Inhalt |
|-------|--------|
| `/assets/app.js` | gzip JS-Bundle |
| `/assets/app.css` | gzip CSS |

## JSON-API

Mutationen erwarten `application/x-www-form-urlencoded` inkl. `csrf_token`.

| Route | Methode | Auth | Beschreibung |
|-------|---------|------|--------------|
| `/api/csrf` | GET | Offen | `{token}` |
| `/api/device` | GET | Offen | Modus, Version, Device-ID, Auth-Flags |
| `/api/chaya` | GET | Session (STA) | `{rx,tx,connected}` |
| `/api/chaya/send` | POST | Session + CSRF | Herz senden (queued) |
| `/api/wifi/status` | GET | AP offen / STA Session | Link-Status |
| `/api/wifi/scan` | GET | AP offen / STA Session | AP-Liste oder **202** |
| `/api/wifi/connect` | POST | CSRF (+ Session wenn Auth an) | Credentials / AP-Test |
| `/api/wifi/connect-status` | GET | AP | Test-State |
| `/api/wifi/connect-commit` | POST | AP + CSRF | Speichern + Reboot |
| `/api/wifi/connect-abort` | POST | AP + CSRF | Test abbrechen |
| `/api/mqtt` | GET/POST | Session + CSRF | Broker-Config (Passwort nie in GET) |
| `/api/mqtt/status` | GET | Session | `{connected}` |
| `/api/pairing` | GET/POST | Session + CSRF | Device-/Partner-ID |
| `/api/settings` | GET/POST | Session + CSRF | Reset-Tage, Web-Auth |
| `/api/auth/login` | POST | CSRF | Session-Cookie setzen |
| `/api/auth/logout` | POST | Session + CSRF | Session beenden |
| `/api/reboot` | POST | Session + CSRF | Neustart (deferred) |
| `/api/update/check` | POST | Session + CSRF | GitHub-OTA-Check |

## Server-Sent Events

| Route | Events |
|-------|--------|
| `/events` | `chaya`, `wifi`, `mqtt` (wie bisher) |

Max. **6** SSE-Clients. Tick alle 500 ms im App-Task.

## Auth / CSRF / Sicherheit

- Optional Web-Auth (`cfg/authEn`), physischer Tastendruck + Code auf E-Ink
- CSRF-Token über `/api/csrf`, in jedem POST als `csrf_token`
- Session-Cookie `chaya_sid` (HttpOnly, SameSite=Strict, 24 h)
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
