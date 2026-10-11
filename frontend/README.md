# Chaya2MQTT Web-UI

The little control room behind each heart: connect it to Wi-Fi, introduce it to
its partner, send a heart, and keep it up to date.

Built with Svelte 5, Vite, Tailwind CSS, and Lucide Icons.

## Development without the ESP32-S3

```bash
npm ci
npm run dev
```

Open `http://127.0.0.1:5173/`. The integrated mock in `mock/` provides `/api/*` and SSE `/events`. Use the simulator bar in the upper-right corner to switch between scenarios (STA, AP setup, MQTT, OTA).

## Tests / production build

```bash
npm test
npm run build
```

`npm run build` creates `dist/` with hashed asset filenames. Production firmware embeds that tree via `rust/chaya-fw/build.rs` (gzip SPA blob from `frontend/dist` when present). Assets are served with `Content-Encoding: gzip` using normal `.js`/`.css` URLs (never `.gz`).
