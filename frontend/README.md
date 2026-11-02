# Chaya2MQTT Web-UI

The little control room behind each heart: connect it to Wi-Fi, introduce it to
its partner, send a heart, and keep it up to date.

Built with Svelte 5, Vite, Tailwind CSS, and Lucide Icons.

## Development without the ESP32-S3

```bash
npm ci
npm run dev:sim
```

Open `http://127.0.0.1:5173/`. The simulator in `sim/` provides `/api/*` and SSE
`/events`. Use the simulator bar in the upper-right corner to switch between
scenarios (STA, AP setup, MQTT, OTA).

Plain Vite without the API mock (no device data):

```bash
npm run dev
```

## Tests / production build

```bash
npm test
npm run build
```

`npm run build` creates `dist/` with hashed asset filenames. Then run the following from the repository root:

```bash
python3 scripts/web/embed_web_assets.py
```

This writes the gzip asset blob, manifest, and assembler stub to `src/web/assets/` (also done automatically by the PlatformIO pre-script). Assets are served with `Content-Encoding: gzip` using normal `.js`/`.css` URLs (never `.gz`).
