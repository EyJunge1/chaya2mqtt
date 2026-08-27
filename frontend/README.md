# Chaya2MQTT Web-UI

Svelte 5 + Vite + Tailwind CSS + Lucide Icons.

## Development without the ESP32-S3

```bash
npm ci
npm run dev
```

Open `http://127.0.0.1:5173/`. The integrated mock in `mock/` provides `/api/*` and SSE `/events`. Use the simulator bar in the upper-right corner to switch between scenarios (STA, AP setup, offline).

## Tests / production build

```bash
npm test
npm run build
```

`npm run build` creates `dist/` with hashed asset filenames. Then run the following from the repository root:

```bash
python3 scripts/embed_web_assets.py
```

This writes the asset blob, manifest, and assembler stub to `src/web/assets/` (also done automatically by the PlatformIO pre-script). The blob is currently stored raw so Safari captive sheets can load it.
