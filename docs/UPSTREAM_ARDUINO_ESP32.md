# Upstream-Kandidaten: chaya2mqtt → arduino-esp32

Sinnvolle Pull-Request-Ideen an [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32), abgeleitet aus Workarounds und IDF-Direktaufrufen in diesem Repo. Kein Anspruch auf Vollständigkeit aller Core-Issues — Fokus: was chaya **konkret umgeht** und sich als klarer Library-/HAL-PR eignet.

## Bereits erledigt / in Arbeit

| Beitrag | Status |
|---------|--------|
| SHA-256 Verify + Header (`setSHA256sum`, `x-SHA256`) | gemerged [#12824](https://github.com/espressif/arduino-esp32/pull/12824) |
| Sidecar-URLs MD5/SHA-256 (`HTTPUpdateChecksum.cpp`) | gemerged [#12848](https://github.com/espressif/arduino-esp32/pull/12848) |
| SHA-512 + Sidecar | offen [#12865](https://github.com/espressif/arduino-esp32/pull/12865) |
| Preferences empty `putString` | offen [#12866](https://github.com/espressif/arduino-esp32/pull/12866) |

Framework-Pin und Nutzung: `platformio.ini`, `docs/OTA.md`, `src/ota/flash.cpp` (`setSHA256sumUrl`). SHA-512 + Sidecar noch offen [#12865](https://github.com/espressif/arduino-esp32/pull/12865) — chaya stellt erst nach Merge um.

In chaya gibt es **keine** vendored Kopien von `HTTPUpdate` / `Update` / `HTTPClient` / `WiFi`. Der einzige Build-Zeit-Patch trifft **GxEPD2** (`scripts/patch_gxepd2_busy_wait.py`) — nicht arduino-esp32.

---

## High

### 1. Redirect-Filter für HTTPClient / HTTPUpdate

- **chaya:** manuelles Redirect-Resolve + Allowlist (SEC-11) in `src/ota/flash.cpp` / `ota_url_allow.h`, danach `HTTPC_DISABLE_FOLLOW_REDIRECTS`
- **Problem:** Redirects ohne Host-/URL-Prüfung; Sidecar-Fetch folgt ebenfalls ungefiltert
- **Upstream:** optionaler `setRedirectFilter(callback)` in HTTPClient, durchreichen an HTTPUpdate inkl. Sidecar. Default `nullptr` = heutiges Verhalten
- **Bleibt App:** die konkrete Hostliste

---

## Medium

### 2. `Preferences::putString("")` liefert 0 — offen [#12866](https://github.com/espressif/arduino-esp32/pull/12866)

- **chaya:** `src/config/nvs_utils.h` (QUAL-04) — leeren String als Erfolg behandeln
- **Core:** nach erfolgreichem `nvs_set_str` Rückgabe `strlen(value)` → `""` sieht aus wie Failure
- **Upstream:** bei leerem Erfolg `1` zurückgeben + Docs/Test (PR oben)

### 3. WiFi soft-reconnect ohne Disconnect-Race

- **chaya:** `esp_wifi_connect()` statt `WiFi.reconnect()` in `src/wifi/wlan_events.cpp`
- **Core:** `STAClass::reconnect()` disconnectet bei bestehender Verbindung zuerst
- **Upstream:** `reconnect(bool forceDisconnect = true)` oder `tryReconnect()` + Doku zum Race
- **Bleibt App:** Soft→Force-Recovery-State-Machine

### 4. `WiFi.setInactiveTime()` fehlt

- **chaya:** `esp_wifi_set_inactive_time(WIFI_IF_STA, …)` in `src/wifi/wlan_boot.cpp`
- **Upstream:** STA-Wrapper analog zu `setSleep` / `setTxPower`

### 5. SNTP: DHCP Option 42 + Fallback ohne `configTime`-Overwrite

- **chaya:** `esp_sntp_*` in `src/wifi/wlan_boot.cpp`
- **Problem:** `configTime()` stoppt SNTP und setzt feste Server — DHCP-NTP geht verloren
- **Upstream:** Helper z. B. `configTimeFromDhcp(fallback…)` oder Docs + API, die DHCP-Slots erhält

### 6. Deep-Sleep EXT1 + RTC-Pulls

- **chaya:** `rtc_gpio_pull*`, `esp_sleep_enable_ext1_wakeup_io`, `esp_deep_sleep_start` in `src/battery/battery.cpp`
- **Core:** im Wesentlichen `ESP.deepSleep(time_us)` / Light-Sleep-GPIO — kein High-Level für EXT1 + RTC-Pulls
- **Upstream:** Sleep-Helfer (`enableExt1Wakeup`, Pull-APIs, `deepSleepStart`)

---

## Low

### 7. `gpio_hold_en` / `gpio_hold_dis` Wrapper

- **chaya:** LED/Power in `src/led/led.cpp`, Reset, Battery
- **Upstream:** `gpioHoldEnable` / `Disable` in der HAL (passt gut zu Deep-Sleep #6)

### 8. HTTPUpdate `const char*` URL-Overloads

- **chaya:** erzwungene `String(resolvedBin)` / `String(resolvedSha)` in `src/ota/flash.cpp` (STAB-07)
- **Upstream:** parallele `const char*`-Overloads (Heap/Stabilität, kein Funktionsbug)

### 9. Optional: `HTTPUpdate::onVerifying()`

- Verify nur indirekt über `onProgress` wenn `done >= total`
- Komfort-Callback, kein Security-Fix

### 10. Docs: WiFi / Preferences und Multithreading

- chaya braucht `g_wifiApiMutex` / `g_nvsMutex`
- Core-APIs sind faktisch nicht multithread-sicher
- **Upstream:** primär Doku/Beispiele (Event-Callback ≠ WiFi-/Preferences-API)

---

## Unklar (erst Root-Cause)

- SoftAP `WIFI_AUTH_WPA2_WPA3_PSK` mit Fallback auf WPA2 in `src/wifi/wlan_boot.cpp` — eher IDF/Chip/Config als Arduino-Bug; ggf. nur Docs/Beispiel

---

## Explizit nicht für arduino-esp32

| Thema | Warum |
|-------|--------|
| GxEPD2 Busy/Reset-Patch | Drittbibliothek |
| ESPAsyncWebServer / AsyncTCP | ESP32Async, nicht Core |
| `esp_mqtt_client` statt PubSubClient | Produktwahl; Core hat kein MQTT |
| I2S/ES8311, SPI vor GxEPD2-Init | Board/App |
| Soft→Force-Reconnect-Policy, OTA-Health-Fenster | App-Policy |
| OTA mark-valid / pending-verify | Core-Hooks `verifyOta` / `verifyRollbackLater` reichen; chaya deferred in `ota.cpp` |
| Captive DNS, Host-Allowlist, CSRF | App-Security |
| TWDT-Unsubscribe während OTA/MQTT-Teardown | App um lange Blocks |
| TX-Power-Throttle während EPD | App; `WiFi.setTxPower` existiert |
| `esp_pm_configure` / DFS | optional; oft `sdkconfig` |
| Code-Signing / Secure Boot | nicht im Scope von chaya (nur Integrität) |
| ADC `analogSetPinAttenuation` vor Read | Espressif by design ([#12197](https://github.com/espressif/arduino-esp32/issues/12197)): erst `analogRead` init’t den Kanal. chaya setzte nur Default `ADC_11db` — Workaround entfernt in `battery.cpp` |

---

## Empfohlene PR-Reihenfolge

1. **Preferences empty putString** — offen [#12866](https://github.com/espressif/arduino-esp32/pull/12866)
2. **WiFi soft reconnect** + **setInactiveTime** (ggf. ein WiFi-PR)
3. **Redirect-Filter** — Security, thematisch an Sidecar-Serie (nach HTTPUpdate-Serie)
4. **SNTP DHCP+Fallback** und **DeepSleep EXT1** — mehr API-Diskussion
5. Low: gpioHold, `const char*` URLs, MT-Docs
6. SHA-512 + Sidecar — offen [#12865](https://github.com/espressif/arduino-esp32/pull/12865); fertigstellen, bevor chaya umstellt

---

## Fazit

Kein zweiter großer Feature-Block wie die Checksum-Serie. Offen in Arbeit: SHA-512 ([#12865](https://github.com/espressif/arduino-esp32/pull/12865)) und Preferences empty string ([#12866](https://github.com/espressif/arduino-esp32/pull/12866)). Nächster sinnvoller non-HTTPUpdate-Kandidat: **WiFi soft-reconnect / setInactiveTime**.
