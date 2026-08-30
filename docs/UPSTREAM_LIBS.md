# Upstream-Prüfung: Drittbibliotheken (außer arduino-esp32)

Core-Kandidaten und offene arduino-esp32-PRs: [UPSTREAM_ARDUINO_ESP32.md](UPSTREAM_ARDUINO_ESP32.md).

## Ergebnis

**Kein weiterer Library-PR.** A/B auf dem Waveshare ESP32-S3-ePaper-1.54G (SKU 34586) mit GxEPD2 1.6.9 zeigte: Stock-Timing reicht für einen SoftAP-Splash-Full-Refresh (~13 s, kein `Busy Timeout!`, Bild vollständig).

| Variante | Reset | Busy-Margin | Serial | Display |
|----------|-------|-------------|--------|---------|
| Beide (Waveshare-Demo) | 200/2/200 | 100 ms | OK | vollständig |
| Nur Reset | 200/2/200 | Stock 1 ms | OK | vollständig |
| Nur Busy | Stock 20/2/2 | 100 ms | OK | vollständig |
| Stock GxEPD2 | 20/2/2 | 1 ms | OK | vollständig |

Der frühere Build-Zeit-Patch (`scripts/patch_gxepd2_busy_wait.py`) ist entfernt. Waveshare-Arduino-Demo (`EPD_1IN54G_Reset` / `ReadBusyH`) bleibt Referenz, ist auf diesem Board aber nicht nötig.

## Explizit nicht upstreamen

| Abhängigkeit | Warum |
|--------------|--------|
| GxEPD2 Busy/Reset | Stock funktioniert; Demo-Timing optional, kein IC-Datenblatt-Zwang |
| ESPAsyncWebServer / AsyncTCP | CSRF, Host-Allowlist, Captive = App-Security |
| `esp_mqtt_client` | Produktwahl; Orchestrierung bleibt App |
| ES8311-Register + PA | Waveshare-Board; kein generischer Arduino-Treiber nötig |
| Nayuki `qrcodegen` | schon Library (vendored, MIT) |
| Adafruit GFX / BusIO | nur Primitives; Layout/Icons = App |

Board/App bleibt in `src/display/display_hw.cpp`: SPI-Pins vor `init()`, Power-Rail GPIO6, `init(0, true, 2, false)` für Waveshare „clever reset“.
