# Web admin

Svelte 5 SPA in the firmware (gzip `/assets/*`). No login. HTTP only on the LAN / SoftAP.

| Mode | URL |
|------|-----|
| SoftAP | Captive portal, `http://chaya2mqtt.local`, `http://4.3.2.1` |
| STA | `http://chaya2mqtt-<deviceId>.local` |

Anyone on that network can change Wi‑Fi/MQTT, reboot, factory-reset, and OTA.

## UI routes

`/`, `/wifi`, `/wifi-testing`, `/mqtt`, `/settings`, `/settings/device`, `/update`

REST: [api/openapi.yaml](api/openapi.yaml). SSE: [api/asyncapi.yaml](api/asyncapi.yaml) (`GET /events`).

```bash
cd frontend && npm ci && npm run dev   # mock device, http://127.0.0.1:5173/
cd frontend && npm test && npm run build
python3 scripts/embed_web_assets.py    # → src/web/assets/ (also a PlatformIO pre-script)
```

More: [frontend/README.md](../frontend/README.md).
