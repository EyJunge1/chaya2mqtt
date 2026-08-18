# Web Administration

The web interface is a **Svelte 5 SPA** (Vite, Tailwind CSS, Lucide) stored in the firmware as a gzip-compressed asset blob. The ESP32-S3 serves HTML/JS/CSS using a manifest lookup and communicates via JSON + SSE; the UI runs in the browser.

## Access

| Mode | URL | Access |
|------|-----|--------|
| AP (setup) | Captive portal / `http://4.3.2.1` | Open `Chaya2MQTT` SoftAP |
| STA | `http://chaya2mqtt.local` (mDNS) | Open on the local network (no login) |

In AP mode, captive portal probes (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, etc.) redirect to `http://4.3.2.1/wifi`.

Hostname: `chaya2mqtt` (constant `kDeviceHostname`).

## Local development (without flashing)

```bash
cd frontend
npm ci
npm run dev
```

The Vite development server starts a **virtual Chaya2MQTT** under `frontend/mock/` with the same `/api/*` and `/events` contracts. The simulator bar (development only) remains visible during boot loading and “Could not connect to the device” states, and controls presets as well as targeted API/SSE errors.

### Simulator presets

| Group | Scenarios |
|-------|-----------|
| Device | `sta-connected`, `boot-unreachable`, `boot-slow`, `sse-disconnected` |
| Network | `offline`, `sta-mqtt-offline`, `sta-mqtt-unconfigured`, `sta-mqtt-unpaired` |
| Wi‑Fi / Setup | `ap-setup`, `wifi-scan-empty`, `wifi-scan-fail`, `ap-test-idle`, `ap-test-testing`, `ap-test-ok`, `ap-test-failed` |
| Update | `update-available`, `update-checking`, `update-busy`, `update-verifying`, `update-rebooting`, `update-error` |

### Fault injection (API/SSE errors)

The toolbar or `POST /api/_mock/fault` (`fault=<key>&enabled=1`, `clear=1`) can reproduce errors without modifying device state:

- Load errors: `device`, `mqtt`, `settings`, `update-status`, `sse`, …
- Action errors: `mqtt-save`, `settings-save`, `reboot`, `factory-reset`, `heart`, `wifi-scan`, `wifi-connect`, `wifi-commit`, `wifi-abort`, `update-check`, `update-install`, …

Additional development endpoints: `POST /api/_mock/scenario`, `POST /api/_mock/reset`, `GET /api/_mock/state`.

```bash
(cd frontend && npm test)
(cd frontend && npm run build)
python3 scripts/embed_web_assets.py
```

## SPA routes (UI)

All UI paths return the same `index.html` (client-side router / SPA fallback):

`/`, `/wifi`, `/wifi-testing`, `/mqtt`, `/settings`, `/update`

The SPA redirects legacy `/pairing` links to `/mqtt`.

Static assets include a Vite content hash and are located under `/assets/*` (Cache-Control: `immutable`). HTML remains `no-cache`.

## JSON-API

Mutations expect `application/x-www-form-urlencoded`, including `csrf_token`.

| Route | Method | Protection | Description |
|-------|--------|------------|-------------|
| `/api/csrf` | GET | Host | `{token}` |
| `/api/device` | GET | Host | Mode, version, device ID; also `apSsid` / `apIp` in AP mode |
| `/api/chaya` | GET | Host + STA | `{rx,tx,connected}` |
| `/api/chaya/send` | POST | CSRF + STA | Send heart (queued) |
| `/api/wifi/status` | GET | Host | Link status including current IP/gateway/netmask/DNS |
| `/api/wifi/config` | GET | Host | Stored WiFi configuration (without password) |
| `/api/wifi/scan` | GET | Host | AP list or **202** |
| `/api/wifi/connect` | POST | CSRF | Credentials + IP mode/DNS/NTP; AP test or STA save + reboot |
| `/api/wifi/connect-status` | GET | AP | Test state |
| `/api/wifi/connect-commit` | POST | AP + CSRF | Save + reboot |
| `/api/wifi/connect-abort` | POST | AP + CSRF | Abort test |
| `/api/mqtt` | GET/POST | Host/CSRF + STA | Broker + partner ID (topics derived; password never included in GET) |
| `/api/mqtt/status` | GET | Host + STA | `{connected}` |
| `/api/settings` | GET/POST | Host/CSRF + STA | Reset days, UI preferences, E-Ink dark mode (`displayDark` / `display_dark`) |
| `/api/reboot` | POST | CSRF + STA | Reboot (deferred) |
| `/api/factory-reset` | POST | CSRF + STA | Delete all NVS data and restart in AP setup mode |
| `/api/update/status` | GET | Host + STA | OTA status (phase, channel, versions, progress) |
| `/api/update/check` | POST | CSRF + STA | GitHub OTA check (`channel=stable\|beta` optional) |
| `/api/update/install` | POST | CSRF + STA | Start confirmed installation |

Machine-readable contracts:

- REST: [openapi.yaml](openapi.yaml) (OpenAPI 3.1)
- SSE: [asyncapi.yaml](asyncapi.yaml) (AsyncAPI 3)

## Server-Sent Events

| Route | Events |
|-------|--------|
| `/events` | `chaya`, `wifi`, `mqtt`, `ota` |

Maximum **6** SSE clients. Tick every 500 ms in the app task.

## CSRF / security

- No web login; the admin UI is accessible to participants on the local network
- CSRF token from `/api/csrf`, included in every POST as `csrf_token`
- Host/origin allowlist; CSP without inline scripts/styles (`script-src 'self'; style-src 'self'`)

## Build integration

1. `frontend/` → `npm run build` → `frontend/dist/`
2. `scripts/embed_web_assets.py` gzip-compresses all distribution files into:
   - `src/web/assets/web_ui.bin` (Blob)
   - `src/web/assets/web_ui_blob.S` (`.incbin` in flash read-only data)
   - `src/web/assets/web_ui_manifest.h` (path, offset, length, MIME, cache class)
3. PlatformIO `pre:scripts/pio_pre_frontend.py` runs this before every firmware build
4. Soft limit: compressed SPA ≤ 350 KiB; OTA slot 1.875 MB
5. Generated blob artifacts are gitignored and regenerated during the build

## Deferred Work

As before, applying MQTT/settings changes and rebooting run as deferred work in the app task. Writing WiFi credentials can write to NVS in the request path.

## Further documentation

- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- Configuration: [CONFIGURATION.md](CONFIGURATION.md)
