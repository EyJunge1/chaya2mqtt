# Findings

Zwei Bugs. MQTT/Heart, Web/SSE, Display/Input und OTA/NVS/Lifecycle ohne Befund.

---

## BUG-NET-01

- **Severity:** Medium
- **Klasse:** Bug
- **Typ:** Lost-Update
- **Ort:** `src/wifi/wlan_recovery.cpp:recoveryNoteRestart` ← `src/wifi/wlan_reset.cpp:wlanControlledRestart` (Network-Task, `wlanRecoveryServiceLoop`)
- **Verletzte Invariante:** Nach einem geclaimten Recovery-Restart muss `rec_rst` in NVS stehen, damit `wlanRecoveryDecide` ab 3 Restarts/Tag `ForcedReassoc` statt `Restart` wählt (STAB-03 / BUG-NET-05).
- **Fehlerpfad (Alltag):**
  1. Gerät hat STA-Credentials und war online (Zeit steht).
  2. WLAN fällt weg (Router aus, Passwort geändert, Reichweite). Kein Setup-AP (`ContinueStaOnly`).
  3. Nach ≥15 min Uptime und ≥10 min Link-Down: `wlanRecoveryServiceLoop` → `wlanControlledRestart(..., recoveryNoteRestart)`.
  4. `systemShutdownTryClaim()` setzt `g_systemShutdownInProgress`.
  5. `recoveryNoteRestart` ruft `app_nvs::writeUInt`/`writeUChar` auf `wifi` — `nvsWriteAllowedAfterLock(true, …, "wifi")` ist `false`. `(void)` verwirft das.
  6. `ESP.restart()`. Nächster Boot: `recoveryRestartsUsedToday()` liest 0.
  7. Nach weiteren ~15 min derselbe Restart.
- **Auswirkung:** Restart-Schleife alle ~15 min bei anhaltendem STA-Ausfall statt max. 3 Restarts/Tag, danach nur Force-Reassoc. Display/LED/MQTT reißen mit jedem Reboot.
- **Kein dokumentiertes Design:** BUG-NET-05 verlangt Persist **nach** erfolgreichem Claim. Das Write-Gate (`cfg`/`wifi`/`mqtt` zu, nur `chaya` offen) ist für Factory/Soft-off, nicht für diesen Hook.
- **Fix:** Nach Claim ungegated schreiben (`ScopedNvsLock` + `Preferences` auf `wifi`/`rec_day`+`rec_rst`, analog Chaya-Flush), oder `rec_*` vor dem Claim schreiben und bei fehlgeschlagenem Claim nicht incrementieren. Write-Fehler loggen, nicht `(void)`.
- **Tests:** `test_recovery_should_note_restart_only_after_claim` prüft nur das Prädikat. `test_nvs_write_allowed_after_lock` bestätigt `wifi`+Shutdown=`false`. Fehlt: afterClaim-Write unter Shutdown muss `rec_rst` erhöhen.

```39:55:src/wifi/wlan_recovery.cpp
void recoveryNoteRestart() {
    time_t nowSec = time(nullptr);
    const uint32_t day = (nowSec > 1700000000) ? static_cast<uint32_t>(nowSec / 86400) : 0U;
    if (day == 0U) {
        return;
    }
    // ...
    (void)app_nvs::writeUInt(kNvsNsWifi, kNvsKeyWifiRecDay, day);
    (void)app_nvs::writeUChar(kNvsNsWifi, kNvsKeyWifiRecRest, n);
}
```

```138:147:src/wifi/wlan_reset.cpp
bool wlanControlledRestart(const char *reasonTag, void (*afterClaim)()) {
    if (g_factoryResetQueued.load(std::memory_order_acquire) || otaBlocksDestructiveAction() ||
        !systemShutdownTryClaim()) {
        // ...
        return false;
    }
    if (afterClaim != nullptr) {
        afterClaim();
    }
```

```12:15:src/config/nvs_write_gate_pure.h
inline auto nvsWriteAllowedAfterLock(bool shutdown, bool chayaSuspended, const char *ns) -> bool {
    if (shutdown && (ns == nullptr || strcmp(ns, kNvsNsChaya) != 0)) {
        return false;
    }
```

---

## BUG-FE-01

- **Severity:** High
- **Klasse:** Bug
- **Typ:** Logic
- **Ort:** `frontend/src/components/WifiSetup.svelte:connect` → `frontend/src/api/client.ts:connectWifi` / `src/web/routes/admin_routes_api_wifi.cpp:parseWifiConfigFromJson`, `handleApiWifiConnectPost` plus `src/wifi/wlan_config.h:wlanBootDecide`
- **Verletzte Invariante:** Ein STA-Save ohne neu eingegebenes Passwort darf das gespeicherte PSK nicht löschen. Nach dem Reboot mit vorhandener SSID darf das Gerät nicht ohne SoftAP offline bleiben.
- **Fehlerpfad (Alltag):**
  1. Gerät im STA, ein Tab, Seite `/wifi`.
  2. `GET /api/wifi/config` liefert kein Passwort; das Feld bleibt leer. Hinweis: „Leer lassen für offene Netze“.
  3. User ändert DNS/NTP/Static oder klickt nur **Speichern & neu starten**.
  4. `connect()` postet `password: ""` (`jsonBody` lässt nur `undefined` weg).
  5. Firmware: `wlanConfigClear` → leeres `pass` (auch bei fehlendem Feld) → NVS → `200 saved_rebooting` → `ESP.restart()`.
  6. Boot: SSID gesetzt → `wlanBootDecide(..., timedOut=true)` = `ContinueStaOnly`. WPA-Heimnetz akzeptiert leeres PSK nicht.
- **Auswirkung:** Gerät offline, kein `Chaya2MQTT`-AP, keine Web-UI. Nur USB-Flash/Serial bzw. Factory-Reset.
- **Kein dokumentiertes Design:** MQTT hat bewusst „leer = behalten“ (`mqtt_pass: password || undefined`, OpenAPI „Omit or empty to keep“). WLAN-OpenAPI hat das nicht; der Hint meint offene Netze, nicht „PSK behalten“. `ContinueStaOnly` gilt für echte STA-Credentials, nicht für versehentlich geleertes PSK.
- **Fix:** STA: leeres Passwort weglassen (wie MQTT); Hint analog „Gespeichert — leer lassen zum Behalten“. Firmware: Feld fehlt (STA + gleiche SSID) → PSK aus NVS mergen. Offenes Netz nur mit bewusst leerem Feld / `open` aus dem Scan.
- **Tests:** `frontend/src/components/WifiSetup.test.ts` prüft `password` nicht. Fehlt: STA-Save ohne Eingabe lässt `password` weg bzw. Firmware mergt NVS-PSK; AP-Setup darf `""` für offen weiter senden.

```213:226:frontend/src/components/WifiSetup.svelte
  async function connect(e: SubmitEvent) {
    e.preventDefault();
    // ...
      const res = await api.connectWifi({
        ssid,
        password,
        mode,
```

```48:54:frontend/src/api/client.ts
function jsonBody(fields: Record<string, string | number | boolean | undefined>): string {
  const body: Record<string, string | number | boolean> = {};
  for (const [key, value] of Object.entries(fields)) {
    if (value === undefined) continue;
    body[key] = value;
  }
```

```60:83:src/web/routes/admin_routes_api_wifi.cpp
    wlanConfigClear(cfg);
    // ...
    if (!parseOptional("password", cfg->pass, sizeof(cfg->pass))) {
        return false;
    }
```

```213:219:src/web/routes/admin_routes_api_wifi.cpp
    if (!wlanSaveConfigToNvs(cfg)) {
        sendErr(req, 500, "save");
        return;
    }
    ESP_LOGI(TAG, "WiFi config saved (STA) ssid=%s — rebooting", cfg.ssid);
    deferredRebootAfterWifiSave();
    sendOk(req, 200, "saved_rebooting");
```

```89:102:src/wifi/wlan_config.h
 * Once STA credentials exist, a timeout must never expose the setup AP again.
inline auto wlanBootDecide(...) -> WlanBootAction {
    if (!hasStaCredentials) {
        return WlanBootAction::StartSetupAp;
    }
    // ...
    if (staConnectTimedOut) {
        return WlanBootAction::ContinueStaOnly;
    }
```

---

## Hotspots ohne Befund

- **MQTT/Heart:** Reserve→Attach→Confirm; late/doppelter/falscher PUBACK; Apply genau einmal; Button/Web über `chayaRequestSend`; Pending-Poll bei Queue-Drop; Lock-Order; Settings-Apply/Unpair killt Client nicht aus dem Event-Task; Subscribe ≠ Publish (kein Self-Echo).
- **WiFi/Network:** `GOT_IP`/`Reconnect`-Flags + Chaya-Pending-Poll; Lock-Order `g_wifiTestMutex` → `g_wifiApiMutex`; WiFi-Event nur Atomics/`NetCmd`; SoftAP-PSK nicht in API/Logs.
- **Web/SSE:** Handler setzen Flags/`NetCmd`; JSON-Kopie in Heap; Host-Allowlist; AP/STA-Gates; SSE-Payload kopiert, Erfolg nur `ENQUEUED`.
- **Display/Input:** SPI/EPD nur Display-Task; ein pending Heart; Audio-Overflow ohne Doppel-Click; Button-ISR nur Notify; LED-Priorität; Soft-off-Latch nach Release; TWDT-Wait nicht auf `nextPage()`.
- **OTA/NVS/Lifecycle:** `otaBlocksDestructiveAction` auf Reset/WiFi-Apply/Flash/Reboot/Soft-off; OTA-TWDT unsubscribe; Health-Fenster 30 s STA; `g_nvsMutex` deckt Preferences; Heart-Flush Soft-off/OTA, Factory flushed absichtlich nicht; Factory-Order Claim→Wipe→RAM→Restart; Init Queues/Mutexes vor Tasks; TLS-Bundle-Mutex.
- **Frontend/Flasher/Vertrag:** SSE vs. Fetch Generation-Gates; Flasher Job-Lock; OpenAPI ↔ Routen ↔ `types.ts` (MQTT-Passwort-Omit passt).

## HIL-Fragen (keine Findings)

- `mqttKillClient` hält `g_mqttClientMutex` während `stop`; `MQTT_EVENT_CONNECTED` kann denselben Mutex bis 2 s blockieren — Timed-Lock, kein dauerhafter Deadlock.
- Broker-PUBACK nach dem 5-s-Timeout (dokumentiertes At-least-once).
- Überlebt `time()` `ESP.restart()` ohne SNTP? Wenn nein, ist `rec_rst` auch nach erfolgreichem Write bei `day==0` unsichtbar.
- Scan + Force-Reassoc im selben Tick (Assoc-Drop).
- OTA-`STA.connect()` soft-only (Absicht).
- PWR nach Soft-off 15 s gehalten: Latch-Cut erst nach stabilem HIGH.
- ESP32Async Client-Liste während `send` ∥ Disconnect.
- `nvs_flash_erase` / HTTPUpdate-Timeout / OTA-Stack 12288 unter Last.

## Gedroppte IDs

Keine. Agent 2: `NET-01` → `BUG-NET-01`. Agents 1, 3, 4, 5: keine Findings.
