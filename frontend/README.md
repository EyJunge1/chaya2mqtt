# Chaya2MQTT Web-UI

React 19 + Vite + Tailwind CSS + Lucide Icons.

## Entwicklung ohne ESP32

```bash
npm ci
npm run dev
```

Öffne `http://127.0.0.1:5173/`. Der integrierte Mock unter `mock/` liefert `/api/*` und SSE `/events`. Über die Simulator-Leiste rechts oben lassen sich Szenarien wechseln (STA, AP-Setup, Offline).

## Tests / Production-Build

```bash
npm test
npm run build
```

`npm run build` erzeugt `dist/`. Anschließend im Repo-Root:

```bash
python3 tools/embed_web_assets.py
```

Das schreibt gzip-PROGMEM-Header nach `src/web/assets/spa_*.h` (auch automatisch via PlatformIO-Pre-Script).
