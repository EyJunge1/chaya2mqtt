# Upstream-Kandidaten: chaya2mqtt → arduino-esp32

Sinnvolle Pull-Request-Ideen an [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32), abgeleitet aus Workarounds und IDF-Direktaufrufen in diesem Repo. Kein Anspruch auf Vollständigkeit aller Core-Issues — Fokus: was chaya **konkret umgeht** und sich als klarer Library-/HAL-PR eignet.

## Bereits erledigt / in Arbeit

| Beitrag | Status |
|---------|--------|
| SHA-256 Verify + Header (`setSHA256sum`, `x-SHA256`) | gemerged [#12824](https://github.com/espressif/arduino-esp32/pull/12824) |
| Sidecar-URLs MD5/SHA-256 (`HTTPUpdateChecksum.cpp`) | gemerged [#12848](https://github.com/espressif/arduino-esp32/pull/12848) |
| SHA-512 + Sidecar | offen [#12865](https://github.com/espressif/arduino-esp32/pull/12865) → [#12868](https://github.com/espressif/arduino-esp32/issues/12868) |
| Preferences empty `putString` | offen [#12866](https://github.com/espressif/arduino-esp32/pull/12866) → [#12869](https://github.com/espressif/arduino-esp32/issues/12869) |
| `WiFi.setInactiveTime` / `getInactiveTime` (STA) | offen [#12867](https://github.com/espressif/arduino-esp32/pull/12867) → [#12870](https://github.com/espressif/arduino-esp32/issues/12870) |

Framework-Pin und Nutzung: `platformio.ini`, `docs/OTA.md`, `src/ota/flash.cpp` (`setSHA256sumUrl`). SHA-512 + Sidecar noch offen [#12868](https://github.com/espressif/arduino-esp32/issues/12868)/[#12865](https://github.com/espressif/arduino-esp32/pull/12865) — chaya stellt erst nach Merge um.

In chaya gibt es **keine** vendored Kopien von `HTTPUpdate` / `Update` / `HTTPClient` / `WiFi`. Drittbibliotheken (GxEPD2 u. a.): [UPSTREAM_LIBS.md](UPSTREAM_LIBS.md).

Die beiden noch offenen **echten** Core-Lücken (kein weiterer Kandidat):

- **`Preferences::putString("")` liefert 0** — nach erfolgreichem `nvs_set_str` Rückgabe `strlen(value)`; chaya behandelt leer als Erfolg in `src/config/nvs_utils.h` (QUAL-04).
- **`WiFi.setInactiveTime()` fehlt** — chaya ruft `esp_wifi_set_inactive_time` in `src/wifi/wlan_boot.cpp`; analog zu `WiFi.setSleep` / `setTxPower`.

---

## Explizit nicht für arduino-esp32

| Thema | Warum |
|-------|--------|
| GxEPD2 Timing | Drittbibliothek; Stock reicht — [UPSTREAM_LIBS.md](UPSTREAM_LIBS.md) |
| ESPAsyncWebServer / AsyncTCP | ESP32Async, nicht Core |
| `esp_mqtt_client` statt PubSubClient | Produktwahl; Core hat kein MQTT |
| I2S/ES8311, SPI vor GxEPD2-Init | Board/App |
| Soft→Force-Reconnect-Policy, OTA-Health-Fenster | App-Policy |
| OTA mark-valid / pending-verify | Core-Hooks `verifyOta` / `verifyRollbackLater` reichen; chaya deferred in `ota.cpp` |
| Captive DNS, Host-Allowlist, CSRF | App-Security |
| Redirect-Filter (`setRedirectFilter`) | Kein fehlendes Feature. HTTPClient hat `HTTPC_DISABLE_FOLLOW_REDIRECTS` + `getLocation()`. chaya in `flash.cpp` / `ota_url_allow.h` nutzt das korrekt (SEC-11). Allowlist bleibt App. |
| Deep-Sleep EXT1 + RTC-Pulls | Vorgesehener Arduino-Pfad ist IDF: Example `ExternalWakeUp.ino`, Fix [#9904](https://github.com/espressif/arduino-esp32/pull/9904). `ESP.deepSleep` ist nur Timer. chaya in `battery.cpp` ist korrekt. |
| HTTPUpdate `const char*` URL-Overloads | `update(client, "https://…")` geht schon über implizite `String`. Extra-Overloads sparen keinen Heap. |
| `HTTPUpdate::onVerifying()` | Komfort. chaya nutzt `onProgress` wenn `done >= total` — reicht. |
| Docs: WiFi / Preferences MT-sicher | Event-Callbacks sind in `wifi.rst` schon gewarnt. Mutexes (`g_wifiApiMutex` / `g_nvsMutex`) sind App-Serialisierung, kein Core-Feature. |
| TWDT-Unsubscribe während OTA/MQTT-Teardown | App um lange Blocks |
| TX-Power-Throttle während EPD | App; `WiFi.setTxPower` existiert |
| `esp_pm_configure` / DFS | optional; oft `sdkconfig` |
| Code-Signing / Secure Boot | nicht im Scope von chaya (nur Integrität) |
| ADC `analogSetPinAttenuation` vor Read | Espressif by design ([#12197](https://github.com/espressif/arduino-esp32/issues/12197)): erst `analogRead` init’t den Kanal. chaya setzte nur Default `ADC_11db` — Workaround entfernt in `battery.cpp` |
| SNTP DHCP Option 42 / `configTime` | kein Bug; `configTime` setzt feste Server by design. DHCP-NTP über IDF (`esp_sntp_*`) ist vorgesehen — chaya in `wlan_boot.cpp` |

---

## Offene PRs

1. **Preferences empty putString** — [#12869](https://github.com/espressif/arduino-esp32/issues/12869) / [#12866](https://github.com/espressif/arduino-esp32/pull/12866)
2. **setInactiveTime** — [#12870](https://github.com/espressif/arduino-esp32/issues/12870) / [#12867](https://github.com/espressif/arduino-esp32/pull/12867)
3. **SHA-512 + Sidecar** — [#12868](https://github.com/espressif/arduino-esp32/issues/12868) / [#12865](https://github.com/espressif/arduino-esp32/pull/12865); fertigstellen, bevor chaya umstellt

---

## Fazit

Keine weiteren Library-PRs. chaya umgeht nur noch die zwei offenen Punkte oben; Redirects, Deep Sleep, URL-`String` und Verify-Callback nutzen vorhandene APIs korrekt.
