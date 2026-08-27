# Browser web flasher

Separate Svelte 5 + Vite + Tailwind 4 app that flashes Chaya2MQTT over USB /
Web Serial via `esptool-js`. The visual tokens match the embedded device admin
UI, but this app has no dependency on device HTTP APIs.

## UI development

```bash
cd flasher
npm ci
npm run dev
```

Open `http://127.0.0.1:4174/`. The Vite development server provides local
Stable/Beta metadata and serves the current
`.pio/build/esp32s3-release/firmware.factory.bin`. Build the release firmware
first if you want to test the actual flash dialog.

## Full local preview with firmware

1. Build a release firmware once so a factory image exists:
   ```bash
   pio run -e esp32s3-release
   ```
2. Stage local release folders:
   ```bash
   mkdir -p /tmp/chaya-releases/v2026.8.1
   cp .pio/build/esp32s3-release/firmware.factory.bin /tmp/chaya-releases/v2026.8.1/
   make flasher RELEASES_DIR=/tmp/chaya-releases
   ```
3. Serve the site (Web Serial needs HTTPS or localhost):
   ```bash
   python3 -m http.server 8080 --directory flasher/_site
   ```
4. Open `http://localhost:8080/`.

The generator copies the compiled `flasher/dist/` app and adds `versions.json`,
channel manifests, and Stable/Beta factory images under `flasher/_site/`.

## Production

`.github/workflows/deploy-pages.yml` builds this site and deploys it to GitHub
Pages once the repository is public and Pages is enabled. Until then, use the
local preview above.
