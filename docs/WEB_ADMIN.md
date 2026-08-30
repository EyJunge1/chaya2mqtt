# Web Administration

The web interface is a **Svelte 5 SPA** (Vite, Tailwind CSS, Lucide) stored in the firmware as a gzip-compressed asset blob. Hashed `/assets/*` files are served with `Content-Encoding: gzip` while keeping normal URL extensions (`.js`/`.css` — never `.gz`, which Safari may treat as a download). Avoid Brotli over plain HTTP. `index.html` is embedded as a C string literal (not a blob slice) so captive browsers always see a real DOCTYPE. The ESP32-S3 serves assets via manifest lookup and communicates via JSON + SSE; the UI runs in the browser.

## Access

| Mode | URL | Access |
|------|-----|--------|
| AP (setup) | Captive portal / `http://chaya2mqtt.local` / `http://4.3.2.1` | WPA2/WPA3 `Chaya2MQTT` SoftAP (WIFI QR on the display) |
| STA | `http://chaya2mqtt-<deviceId>.local` (mDNS) | Open on the local network (no login) |

> **Security warning (product model):** There is intentionally **no web login**. Anyone who can
> reach the device on the LAN (or who joins the SoftAP) can change Wi‑Fi/MQTT settings, reboot,
> factory-reset, and trigger OTA. Treat guest Wi‑Fi and untrusted LAN hosts accordingly.
>
> Admin traffic is **cleartext HTTP** only — Wi‑Fi and MQTT passwords are sent unencrypted on the
> local link (MITM risk on hostile networks). Prefer SoftAP provisioning or a trusted home LAN.
>
> SoftAP uses a **24-character alphanumeric WPA-PSK** (WIFI QR only — no manual typing
> fallback). That resists casual offline brute-force during setup.
>
> In **AP (captive) mode**, Host/Origin allowlisting is limited to the setup IP (`4.3.2.1`) and
> hostname `chaya2mqtt` / `.local`. Captive probe routes remain separate. After joining SoftAP,
> treat the radio link as the trust boundary. In STA mode the device hostname/IP allowlist is enforced.

In AP mode, captive portal probes (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, etc.) redirect to `http://4.3.2.1/` (root serves the Wi-Fi setup UI).

The AP hostname remains `chaya2mqtt`. In STA mode the DHCP and mDNS hostname is derived from the stable device ID, for example `chaya2mqtt-a1b2c3`. This lets multiple devices share one LAN without hostname collisions. The dashboard shows the resulting `.local` address and device ID; direct access through the device's unique IP remains supported.

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
| Connection | `sta-connected`, `sse-disconnected`, `device-unreachable` |
| Dashboard | `battery-full`, `battery-medium`, `battery-low`, `battery-critical`, `heart-busy`, `heart-send-fail` |
| MQTT | `sta-mqtt-offline`, `sta-mqtt-unconfigured`, `sta-mqtt-unpaired`, `mqtt-no-auth`, `mqtt-load-fail`, `mqtt-save-fail` |
| Settings | `settings-load-fail`, `settings-save-fail`, `settings-nvs-fail`, `settings-reboot-fail`, `settings-factory-reset-fail` |
| Wi‑Fi | `wifi-weak`, `wifi-static`, `wifi-sta-save-fail` |
| AP setup | `ap-setup`, `wifi-scan-empty`, `wifi-scan-fail`, `ap-test-idle`, `ap-test-testing`, `ap-test-ok`, `ap-test-failed`, `wifi-test-start-fail`, `wifi-test-save-fail`, `wifi-test-retry-fail`, `wifi-test-abort-fail` |
| Update | `update-uptodate`, `update-available`, `update-beta`, `update-checking`, `update-busy`, `update-progress-unknown`, `update-verifying`, `update-rebooting`, `update-error`, `update-check-fail`, `update-install-fail`, `update-status-fail` |

### Fault injection (API)

`POST /api/_mock/fault` (`fault=<key>&enabled=1`, `clear=1`) can still toggle individual fault keys for automated tests. Prefer the scenario presets above in the simulator toolbar.

Additional development endpoints: `POST /api/_mock/scenario`, `POST /api/_mock/reset`, `GET /api/_mock/state`.

```bash
(cd frontend && npm test)
(cd frontend && npm run build)
python3 scripts/embed_web_assets.py
```

## SPA routes (UI)

All UI paths return the same `index.html` (client-side router / SPA fallback):

`/`, `/wifi`, `/wifi-testing`, `/mqtt`, `/settings`, `/settings/device`, `/update`

The SPA redirects legacy `/pairing` links to `/mqtt`.

Static assets include a Vite content hash and are located under `/assets/*` (Cache-Control: `immutable`). HTML remains `no-cache`.

## JSON API

Mutations expect `application/x-www-form-urlencoded`, including `csrf_token`.

| Route | Method | Protection | Description |
|-------|--------|------------|-------------|
| `/api/csrf` | GET | Host | `{token}` |
| `/api/device` | GET | Host | Mode, version, device ID, `batteryMv` / `batteryPct`; also `apSsid` / `apIp` in AP mode |
| `/api/chaya` | GET | Host + STA | Display deltas `{rx,tx}` plus MQTT `{connected,configured,paired}` |
| `/api/chaya/send` | POST | CSRF + STA | Send heart (queued; requires broker + partner) |
| `/api/wifi/status` | GET | Host | Link status including current IP/gateway/netmask/DNS |
| `/api/wifi/config` | GET | Host | Stored WiFi configuration (without password) |
| `/api/wifi/scan` | GET | Host | AP list or **202** |
| `/api/wifi/connect` | POST | CSRF | Credentials + IP mode/DNS/NTP; AP test or STA save + reboot |
| `/api/wifi/connect-status` | GET | AP | Test state |
| `/api/wifi/connect-commit` | POST | AP + CSRF | Save + reboot |
| `/api/wifi/connect-abort` | POST | AP + CSRF | Abort test |
| `/api/wifi/connect-retry` | POST | AP + CSRF | Retry failed test with same credentials |
| `/api/mqtt` | GET/POST | Host/CSRF + STA | Broker + partner ID (topics derived; password never included in GET) |
| `/api/mqtt/status` | GET | Host + STA | `{connected}` |
| `/api/settings` | GET/POST | Host/CSRF + STA | Reset days, UI preferences, TX/RX sound enable, per-kind volume/Hz/ms, quiet hours |
| `/api/reboot` | POST | CSRF + STA | Reboot (deferred) |
| `/api/factory-reset` | POST | CSRF + STA | Delete all NVS data and restart in AP setup mode |
| `/api/update/status` | GET | Host + STA | OTA status (phase, channel, versions, progress) |
| `/api/update/check` | POST | CSRF + STA | GitHub OTA check (`channel=stable\|beta` optional) |
| `/api/update/install` | POST | CSRF + STA | Start confirmed installation |

`rx` and `tx` are the current display deltas (absolute counter minus its saved
baseline), not the absolute MQTT counters. The web API returns the uncapped deltas;
the E-Ink renderer formats its compact counter display separately. `connected`
reports the live MQTT connection, while `configured` reports whether a broker has
been configured. `paired` is true when a partner device ID is set; heart send (web and
device button) and the E-Ink heart view require both broker and partner.

STA-only routes return `400 {"ok":false,"error":"ap_mode"}` in setup-AP mode.
The AP-only connect-status route returns `400 {"ok":false,"error":"not_ap"}` in
STA mode. Operations that cannot safely proceed during another update, shutdown,
or configuration snapshot may return `503` with `busy` or `shutdown`.

Machine-readable contracts:

- REST: [openapi.yaml](openapi.yaml) (OpenAPI 3.1)
- SSE: [asyncapi.yaml](asyncapi.yaml) (AsyncAPI 3)

## Server-Sent Events

| Route | Events |
|-------|--------|
| `/events` | `chaya`, `wifi`, `mqtt`, `ota`, `device` |

Maximum **6** SSE clients. App-task poll remains ~500 ms, but SSE gather runs only on producer dirty bits or an **8 s keepalive** (PERF-03).

**SSE CORS (AP mode):** `/events` does **not** set `Access-Control-Allow-Origin`. The SPA connects with same-origin `EventSource("/events")` and CSP `connect-src 'self'`. Missing ACAO is intentional defense-in-depth against cross-origin EventSource (e.g. page on `.local` talking to `4.3.2.1`). Do not add wildcard ACAO.

## CSRF / security

- No web login; the admin UI is accessible to participants on the local network (see warning under
  **Access** — LAN participants can fully control the device)
- Admin is HTTP-only: credentials travel in cleartext on the LAN
- SoftAP PIN is 8 decimal digits (shown on the E-Ink WIFI QR)
- `/api/csrf` returns `token` plus `expiresInSeconds`; tokens rotate lazily every 24 hours and the
  previous token remains valid for a 5-minute grace period
- Every POST includes `csrf_token`. The frontend performs a single-flight refresh and one retry
  only for idempotent settings/check operations; heart send, WiFi commit, reboot, factory reset,
  and OTA install are never repeated automatically
- Host/origin allowlist in STA mode; in AP/captive mode Host/Origin checks are skipped so captive
  browsers can complete setup (see Access warning / SEC-10)
- CSP without our inline scripts/styles (`script-src 'self'` plus exact captive-browser helper hashes; `style-src 'self'`)
- Expensive/mutating routes (Wi‑Fi scan/connect, reboot, factory reset, OTA check/install) apply a
  short per-route minimum interval and may respond with **429** `rate_limit`

## Build integration

1. `frontend/` → `npm run build` → `frontend/dist/`
2. `scripts/embed_web_assets.py` packs `/assets/*` (gzip) into:
   - `src/web/assets/web_ui.bin` (gzip blob; URLs keep `.js`/`.css`)
   - `src/web/assets/web_ui_blob.S` (`.incbin` in flash read-only data)
   - `src/web/assets/web_ui_manifest.h` (path, offset, length, MIME, cache class + `kWebUiIndexHtml` literal)
3. PlatformIO `pre:scripts/pio_pre_frontend.py` runs this before every firmware build
4. Hard build budget: compressed SPA blob ≤ 350 KiB (embed script fails if exceeded); OTA slot ~3.75 MB
5. Generated blob artifacts are gitignored and regenerated during the build

## Deferred Work

Applying MQTT/settings changes and rebooting run as deferred work in the app task. Writing WiFi credentials can write to NVS in the request path.

## Further documentation

- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- Configuration: [CONFIGURATION.md](CONFIGURATION.md)
