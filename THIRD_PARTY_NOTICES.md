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
- [esptool-js](https://github.com/espressif/esptool-js) — Apache License 2.0

The vendored QR encoder in `src/display/qr/` is the
[Nayuki QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library)
under the MIT License. Its copyright and full license notice are retained at the top of
`qrcodegen.c` and `qrcodegen.h`.

Exact versions are pinned in `platformio.ini`, `frontend/package-lock.json`, and
`flasher/package-lock.json`. Installed dependency packages contain their complete license texts.
