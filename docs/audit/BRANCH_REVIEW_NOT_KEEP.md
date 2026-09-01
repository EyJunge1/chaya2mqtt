# Branch-Review: was nicht sinnvoll ist

Unabhängige Prüfung von `audit-remediation` vs. `main` durch vier Reviews (Sicherheit, Stabilität/OTA/WiFi, Architektur/Perf, Frontend/Docs). Das Canvas bleibt unverändert. Hier nur Punkte, die **übertrieben, falsch oder unfertig** sind — der Rest der Branch ist überwiegend behaltenswert.

Stand: geprüft nach den Fixes unter „Erledigt“.

---

## Erledigt

### Tote UPSTREAM-Links

Verweise in `docs/README.md` und `docs/DISPLAY.md` entfernt. Dateien bleiben gelöscht.

### SoftAP-Doku: PIN vs. 24-Zeichen QR-only

README/WEB_ADMIN/ARCHITECTURE/TESTING sprechen QR-only / alphanumerische PSK. NVS-Key `ap_pin` in `CONFIGURATION.md` ist nur der historische Schlüsselname.

### OpenAPI-Version

README und WEB_ADMIN sagen wieder OpenAPI 3.2, passend zu `docs/openapi.yaml` (`3.2.0`).

### MQTT-Save: Apply-Wait

GET `/api/mqtt` liefert `applyPending`. POST setzt das Flag; Network-Task räumt es nach Apply/NVS. `MqttPage` pollt wie Settings, TLS-Toast hängt am abgeschickten Formular.

### WiFi-Scan-Refresh

GET `/api/wifi/scan` ist read-only (Cache/Poll). POST invalidiert den Cache und kickt einen Sweep. Refresh-Button ruft `startWifiScan()` (POST), Mount hydriert nur GET.

### Soft-Off-Timeout — kein sofortiges Wiederaufwachen

Timeout schneidet den Latch und wartet weiter. `batteryPowerOffAndSleep()` armiert EXT1 nur wenn PWR nicht LOW ist.

---

## Noch offen (echte Restbugs)

### SegmentedControl: Fokus bleibt auf altem Radio

Pfeile/Home/End ändern den Wert, bewegen den Fokus nicht. Nach dem Wechsel hat das fokussierte Element `tabindex="-1"`.

### Soft-Off-Timeout 15 s wirkt nicht wie gedacht — erledigt

`waitForPwrRelease()` schlief nach 15 s trotzdem. Wake ist `EXT1 ANY_LOW` auf PWR (pegelgesteuert). Klebt der Taster LOW: Latch aus → Deep-Sleep → sofort wieder wach → Latch wieder HIGH.

**Umgesetzt:** Timeout schneidet den Latch und wartet weiter (KEEP_AWAKE). `batteryPowerOffAndSleep()` armiert EXT1 nur wenn PWR nicht LOW ist.

---

## Überengineert (behalten, aber entschlacken — nicht revertieren)

### SPA-CORS-Reflector

`Access-Control-Allow-Origin: *` weg: richtig. Der Ersatz (~45 Zeilen, Origin-Liste + `http://<Host>`-Synthese) ist unnötig: Vite entfernt `crossorigin` und liefert klassisches `<script defer>`. Same-origin braucht kein ACAO. `/events` und APIs setzen keines.

**Nicht** zu `*` zurück. **Besser:** ACAO auf SPA-Assets weglassen.

### MQTT Username/Password-Syntax (SEC-09)

Nur Admin-API, nicht NVS-Load. Username nur printable ASCII blockt UTF-8-Broker-User. „Embedded-NUL“ gilt für `c_str()` nicht. Kein Exploit verhindert.

**Besser:** Control-Chars streichen reicht; ASCII-only Username entfernen.

### Web-Server-Hook-Registry

Ziel richtig (WiFi/Network ohne `web/`). Vier Funktionspointer + stille No-Ops wenn `Install` fehlt: Zeremonie. Boot-Reihenfolge RegisterRoutes → setupWiFi → Begin behalten.

**Besser:** Deklarationen in `async/` / dünnem Header, eine Implementierung, Link-Fehler statt No-Op.

### Button `performSoftOff`-Hook

`requestSend` und `softOffAllowed` lohnen sich (brechen button→mqtt/ota). `performSoftOff` fällt auf `batteryPowerOffAndSleep()` zurück; Battery bleibt sowieso Include.

### SSE RSSI-Hysterese 3 dBm

Dirty-Bits + 8 s Keepalive: sinnvoll. Keepalive markiert WiFi sowieso dirty → Hysterese filtert nichts. Ungenutztes `webEventsMarkDirty` ebenfalls streichen.

### OTA-Redirect-Walker (Allowlist behalten)

CDN-Allowlist + kein blindes Follow-Redirects: **behalten**. Der Walker in `flash.cpp` ist zu schwer:

- HEAD→GET bei 403 lädt `firmware.bin` schon in der Resolve-Phase (Heap/WDT)
- Signierte CDN-URLs können zwischen Probe und `HTTPUpdate` ablaufen
- Neue GitHub-CDN-Hosts brechen OTA hart

**Tun:** Location nur parsen und Host prüfen; kein Body-GET zum Bestätigen.

### clang-tidy-Pure-Check in CI

Script exit 0 wenn `clang-tidy` fehlt. CI installiert es nicht → Gate ist tot.

**Tun:** im Job installieren oder nicht als Quality-Gate verkaufen.

`npm audit` in CI ist ok (kann bei Transitives nerven).

---

## Strittig / Produkt, kein klarer Revert

### SoftAP ohne WPA2-only-Fallback

Nur noch `WIFI_AUTH_WPA2_WPA3_PSK`. Mixed-Mode erlaubt weiter WPA2-Clients — **kein Security-Gewinn**. Fallback war Bring-up-Absicherung, falls `softAP(mixed)` fehlschlägt.

- Review Sicherheit: Fallback wiederherstellen
- Review Stabilität: Produktentscheidung, USB-Recovery bleibt

**Empfehlung:** Fallback zurück (Mixed zuerst, WPA2-only wenn `softAP` fehlschlägt). Wenig Code, weniger Brick-Setup.

### OTA-Health braucht STA + Recovery-Restart

`verifyRollbackLater` + STA-Fenster: Anti-Brick, **behalten**. Zusammenspiel: Image bleibt `PENDING_VERIFY` ohne STA. Recovery startet nach ~15 min Uptime + 10 min Down neu → Bootloader rollt ein **gutes** Image zurück, wenn nur der Heim-AP tot ist.

Kein Overengineering, aber bewusstes Fehl-Rollback-Risiko. Tageslimit 3 Restarts dämpft das; ohne NTP greift der Cap nicht.

---

## Bewusst nicht auf dieser Liste

Diese Punkte sind sinnvoll und sollten **nicht** zurück:

SoftAP 24-Zeichen-PSK, Host-Allowlist, CSRF nur Body, Flasher SHA-256, Display-Unknown vor Refresh, async Chaya-Publish, `verifyRollbackLater`, OTA-Akku-Gate, Task-WDT explizit, OTA-Stack 12K, GitHub-JSON auf Heap/PSRAM, WLAN Soft-Reconnect + Restart-Cap, Settings-NVS-Retry, `configCopyUi*`, mqttCfg-Atomics, Lifecycle-Flag nach `async/`, SSE Dirty-Bits, `s_staLinkOk` Atomic, MQTT CONNECT ein Mutex, `/api/bootstrap`, Theme `system`, CSRF vor Reboot/Factory-Reset, Rate-Limit-Endstand (weg), `gpio_hold` weg, Battery-ADC-Workaround weg.

---

## Reihenfolge zum Aufräumen

1. OTA-Walker entschlacken (Allowlist lassen)
2. SoftAP WPA2-Fallback wieder
3. Optional entschlacken: SPA-ACAO, MQTT-ASCII-User, Web-Hook-Registry, RSSI-Hysterese, SegmentedControl-Fokus, clang-tidy CI
