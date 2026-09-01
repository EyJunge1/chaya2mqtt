# Third-party software

Chaya2MQTT uses third-party software under its own licenses. The project license in
[`LICENSE`](LICENSE) does not replace those licenses.

Runtime components include:

- [GxEPD2](https://github.com/ZinggJM/GxEPD2) — GNU GPL v3
- [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) — GNU LGPL v3 or later
- [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) — GNU LGPL v3 or later
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) — BSD
- [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) — MIT
- [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) and
  [ESP-IDF](https://github.com/espressif/esp-idf) — see their component license files
- [Svelte](https://github.com/sveltejs/svelte) — MIT
- [Lucide](https://github.com/lucide-icons/lucide) — ISC
- [Tailwind CSS](https://github.com/tailwindlabs/tailwindcss) — MIT (generated CSS in the
  embedded SPA and the web flasher)
- [esptool-js](https://github.com/espressif/esptool-js) — Apache License 2.0

The vendored QR encoder in `src/display/qr/` is the
[Nayuki QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library)
(C sources, MIT License). The `.c` file matches upstream
[`8329a7108fc22be3e1eec0a9f9318978579e3621`](https://github.com/nayuki/QR-Code-generator/commit/8329a7108fc22be3e1eec0a9f9318978579e3621)
(master after tag v1.8.0). Copyright and the full license notice are retained at the top of
`qrcodegen.c` and `qrcodegen.h`.

Host tests use [Unity](https://github.com/ThrowTheSwitch/Unity) (MIT), pinned as
`throwtheswitch/Unity@2.6.1` in `platformio.ini` (`env:native` / `env:native-asan`).
The optional MQTT simulator (`scripts/simulator.py`) needs
[paho-mqtt](https://github.com/eclipse/paho.mqtt.python) (EPL-2.0 / EDLv1.0) when you run it.

E-Ink Lucide bitmaps are generated from Lucide **0.468.0**; the web UI and flasher use
`@lucide/svelte` **1.34.0**. See [DISPLAY.md](docs/DISPLAY.md).

Exact versions are pinned in `platformio.ini`, `frontend/package-lock.json`, and
`flasher/package-lock.json`. Installed dependency packages contain their complete license texts.
