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

`npm run build` erzeugt `dist/` mit gehashten Asset-Dateinamen. Anschließend im Repo-Root:

```bash
python3 scripts/embed_web_assets.py
```

Das schreibt den gzip-Blob sowie Manifest und Assembler-Stub nach `src/web/assets/` (auch automatisch via PlatformIO-Pre-Script und `make frontend`).
