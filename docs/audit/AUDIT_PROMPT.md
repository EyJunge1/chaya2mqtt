# Audit-Prompt: chaya2mqtt Vollständiger Qualitäts-Audit

Diesen Prompt 1:1 an einen Orchestrator-Agenten übergeben (Agent-Mode).

---

Du bist der Orchestrator für einen vollständigen Qualitäts-Audit von `chaya2mqtt`.

## Harte Regeln für Subagents

- Starte MEHRERE parallele Subagents (Task-Tool) für thematische Teilbereiche.
- Jeder Subagent MUSS mit dem Modell `cursor-grok-4.5-high-fast` laufen (Parameter `model: "cursor-grok-4.5-high-fast"`).
- KEINE anderen Modelle verwenden (kein inherit, kein Claude, kein GPT, kein Composer).
- Wenn das Modell nicht verfügbar ist: abbrechen und melden — nicht auf ein anderes Modell ausweichen.
- Subagents arbeiten read-only (keine Code-Änderungen, keine Commits, keine PRs).
- Du aggregierst am Ende und schreibst die Markdown-Reports.

## Ziel

Vollständiger Audit mit **Pflichtschwerpunkten**:

1. **Stromsparen / Energieeffizienz** (Modem-Sleep, Display, Polling, Idle, Battery)
2. **Memory Leaks & Heap-Stabilität** (lange Uptime, Reconnect, OTA, Web, Frontend)
3. **Race Conditions allgemein** (Tasks, ISRs, Shared State, Queues, Lifecycle — Korrektheit/Stabilität)
4. **Sicherheitsprobleme** (Auth, CSRF, Injection, Secrets, OTA-Trust, XSS)
5. **Security Race Conditions** (TOCTOU, Session/CSRF-Races, concurrent Auth/Config — ausnutzbar)
6. **Performance-Optimierungen** (Hot Paths, Kopien, Polling, Bundles, Chattyness)
7. Zusätzlich: Codequalität, ESP-Stabilität, Frontend↔Firmware-Contract, Tests/CI

**Race-Zuordnung:** Korrektheit/Crash/Corruption → `STAB` (bzw. `FE` im UI); ausnutzbare Auth/CSRF/Privilege-Fenster → `SEC`. Bei Überlappung: ein Finding + Querverweis, nicht doppelt zählen.

Findings nur belegen (Datei + Zeile). Keine spekulativen Fixes umsetzen — nur Reports.
Keine kosmetischen Nitpicks ohne Impact. Keine Massen-Style-Nits (Formatierung), außer sie maskieren Bugs.

## Repo-Kontext

ESP32-S3 Firmware (PlatformIO/ESP-IDF), MQTT-Bridge, Admin-Web-UI (Svelte), OTA, WiFi/Captive Portal, NVS, FreeRTOS-Tasks, Display/LED/Battery/Button, Flasher, native Tests + Frontend Unit/E2E.
Bereiche: `src/`, `frontend/`, `test/`, `flasher/`, `scripts/`, `sim/`, `docs/`, `.github/`, `platformio.ini`, `partitions_*.csv`, `sdkconfig.defaults`.

ESP-Realität beachten: begrenzter RAM/Flash, lange Uptime, schlechtes WLAN, **Batterie/Strombudget**, Brownout.

## Vorgehen (Orchestrator)

1. Kurz selbst Repo-Orientierung (Struktur, kritische Pfade: Boot, Tasks, Netz, MQTT, Web/API, OTA, Power/Idle, Frontend↔Firmware-Contract).
2. Sofort alle **7 Agents** parallel starten mit `model: "cursor-grok-4.5-high-fast"`.
3. Auf alle Agents warten, Findings deduplizieren (gleiche Root Cause → ein Finding, Querverweis; Power+Perf, Stab+Leak, Stab-Race+SEC-Race oft verwandt).
4. Thematische Markdown-Dateien unter `docs/audit/` schreiben (Ordner anlegen falls nötig).
5. Mir die Pfade der geschriebenen Dateien + kurze Executive Summary zurückgeben.
6. Keine Code-Änderungen außerhalb von `docs/audit/`.

## Agent-Aufteilung (genau diese 7, parallel)

### Agent 1 — Firmware Stability, Memory Leaks & Race Conditions

Prefix: `STAB`

**Pflicht: Memory Leaks UND allgemeine Race Conditions** explizit und systematisch prüfen.

Fokus Stability: FreeRTOS (Stack-Größen, Prioritäten, Blocking in ISRs, Task-Starvation, Watchdog TWDT/IWDT, Deadlocks/Priority Inversion), Timing (`delay` vs Tick, Busy-Wait, Drift), Brownout/Low-Battery, Boot-Loops, Soft-Reset, Partitionstabelle/Flash/Binary-Size.

**Memory-Leak-Checkliste (muss abgearbeitet werden):**

- Alloc ohne Free / fehlende Ownership bei Fehlerpfaden (`goto`/`return` mid-function)
- Event-Handler, Callbacks, Timers, Sockets, Tasks, Queues, Semaphores: Registrierung ohne Deregistrierung
- Reconnect-/Retry-Schleifen (WiFi, MQTT, HTTP, OTA): wachsende Allokationen, String-Akkumulation, doppelte Session-Objekte
- OTA / Firmware-Download: Buffer, HTTP-Clients, File-Handles nach Abbruch/Timeout
- WiFi Scan / Captive Portal: Scan-Ergebnisse, DNS/HTTP-Buffers, AP-Mode-Übergänge
- Webserver: Response-Bodies, Chunk-Buffers, Client-State nach Disconnect; Streaming ohne Bound
- NVS / Config: große temporäre Strings, doppelte Deserialize-Buffers
- C++: `new`/`malloc`/`ps_malloc`, `String`/`std::string` in Loops, `std::vector` growth ohne `shrink`, dangling pointers nach Move
- Lange Uptime: periodische Allokationen (Heartbeat, Status-JSON, Logging), Fragmentierung durch häufige klein/groß Mix-Alloks
- Fehlende Bounds: Stack-Allokationen großer Arrays, Rekursion, unbounded Queues

**Race-Condition-Checkliste allgemein (muss abgearbeitet werden):**

- **Shared Globals / Module-State**: non-atomic Reads/Writes über Tasks/ISRs hinweg (Flags, Counters, Structs, Pointers, `String`)
- **Fehlende / falsche Sync**: Mutex/Spinlock/Critical Section fehlt, zu kurz, zu lang, oder falsch geschachtelt (Deadlock)
- **ISR vs Task**: Arbeit/Allokation/Logging in ISR; Shared Data ohne `portENTER_CRITICAL` / atomics; Queue von ISR ohne `FromISR`
- **Init / Teardown Races**: Task startet bevor Init fertig; Stop/Delete während anderer Task noch zugreift; Double-Start / Double-Free
- **Pointer / Lifetime**: Use-after-free nach Disconnect, OTA, Config-Reload; dangling Callback auf zerstörtes Objekt
- **Producer/Consumer**: Queue Overflow/Underflow, verloren gegangene Events, fehlende Backpressure, non-atomic Multi-Feld-Updates
- **Check-then-act (nicht-Security)**: Mode/Connected-Flag prüfen, dann handeln während Flag wechselt → falscher Pfad, Crash, inkonsistenter State
- **WiFi / MQTT / Network Lifecycle**: Connect/Disconnect/Reconnect parallel zu Publish/Scan/HTTP; Client-Handle während Use invalidiert
- **Config / NVS**: Writer vs Reader ohne Snapshot; partial Struct sichtbar; Reload während laufender Operation
- **Display / Button / LED / Battery**: UI-Task vs Input-ISR vs Network-Task auf gemeinsamen Buffers/State
- **Webserver Concurrent Requests**: Handler teilen Globals ohne Lock; Response-State von Request A von B überschrieben
- **Time / Tick Races**: `millis()`-Fenster, Reentrancy in Timern/Periodics

Jeder allgemeine Race-Finding: **Trigger** (welche Tasks/ISRs/Requests), **Shared State**, **fehlende Synchronisation**, **Symptom** (Crash, Corruption, Lost Update, Inkonsistenz). Security-Ausnutzung? → zusätzlich `SEC`-Querverweis.

Pfade: `src/async/`, `src/diag/`, `src/wifi/`, `src/mqtt/`, `src/ota/`, `src/config/`, `src/display/`, `src/battery/`, `src/led/`, `src/button/`, `src/heart/`, `src/network/`, `src/web/`, `src/main.cpp`, `partitions_*.csv`, `sdkconfig.defaults`.

### Agent 2 — Security & Security Race Conditions

Prefix: `SEC`

**Pflicht: klassische Sicherheitslücken UND Security Race Conditions** getrennt belegen.
Allgemeine (nicht-ausnutzbare) Concurrency-Bugs gehören zu Agent 1 (`STAB`) — hier nur Races mit **Security-Impact**.

Fokus Security: Auth/Session für Admin-API, CSRF (Token-Bindung, SameSite/Origin/Host-Checks), Injection (MQTT-Topics/Payloads, JSON, Query/Path, Host-Header, Open Redirect), Secrets in Repo/Logs/Frontend-Bundle (Passwörter, Tokens, WiFi, MQTT Creds), TLS (Zertifikatsprüfung, insecure Defaults), OTA (untrusted URL, Allowlist, Signatur/Integrität, partial update, Downgrade), XSS/CSRF im SPA + API, Path Traversal bei Assets, Rate-Limits/Brute-Force auf Login & sensible Endpoints, Least Privilege in CI/Scripts/Flasher.

**Security-Race-Condition-Checkliste (muss abgearbeitet werden):**

- **TOCTOU**: Check-then-act bei Auth, CSRF, Host-Validate, Rate-Limit, OTA-Allowlist, Config-Write (Zustand zwischen Check und Use änderbar?)
- **Session / CSRF**: Token-Rotation während paralleler Requests; Token ungültig vs Request noch in Flight; Double-Submit / Replay-Fenster
- **Concurrent Auth**: parallele Logins, Logout während aktiver API-Calls, Session-Fixation bei Race Login↔Cookie-Set
- **Config / NVS Races (security)**: Credentials/ACL mid-request geändert; partial secret sichtbar; schwächere Config gewinnt Race
- **OTA Races (security)**: zweiter Update-Request / Downgrade-Fenster; Integrity-Check vs Flash-Write Race
- **WiFi / Portal Races (security)**: Credential-Change während Connect; Captive vs STA Privilege-Fenster
- **MQTT Races (security)**: Credential-Update mid-session; Topic-ACL vs Subscribe Race
- **Rate-Limit / Brute-Force Races**: Counter nicht atomar; Fenster-Reset race; Bypass durch parallele Connections
- **Frontend↔API**: abgebrochene Requests, stale responses überschreiben neuere Auth/Settings-State
- **Privilege / Mode Races**: Admin-only Aktion nach Mode-Wechsel (AP↔STA, unlocked↔locked) noch ausführbar

Jeder SEC-Race-Finding: Trigger (welche zwei Pfade), Shared State, fehlende Synchronisation, **ausnutzbares Fenster** beschreiben.

Pfade: `src/web/` (csrf, host_validate, rate_limit, middleware, routes, OTA-APIs), `src/ota/`, `src/tls/`, `src/mqtt/`, `src/wifi/`, `src/config/`, `frontend/src/api/`, `scripts/`, `flasher/`, `.github/`.

### Agent 3 — Firmware Quality & Architecture

Prefix: `QUAL`

Fokus: Duplikation, tote Pfade, inkonsistente Error-Handling-Patterns, Modulgrenzen (wifi/mqtt/web/ota/config), zyklische Dependencies, Naming, Konstanten vs Magic Numbers, Header-Hygiene, C++ Ownership/Lebensdauer/UB/Buffer-Overflows/Integer-Overflow/unchecked snprintf, Logging (PII/Secrets, Spam, Levels), Config-Drift zwischen `app_config`, NVS-Keys, Frontend-Types, Docs.

Pfade: gesamtes `src/`, relevant `docs/` nur für Drift zu Code.

### Agent 4 — Performance & Optimierungen

Prefix: `PERF`

**Pflicht: konkrete Optimierungschancen mit messbarem Impact**, nicht nur „könnte langsamer sein“.

Fokus: Hot Paths (MQTT Publish, WiFi Events, Web JSON, Display Refresh), unnötige Kopien, große Temporaries, JSON-Parsing-Kosten, Polling vs Event-driven, zu aggressives Reconnect/Publish, Frontend unnötige Re-Renders, große Bundles, fehlendes Code-Splitting, schwere Sync-Arbeit im UI-Thread, Netzwerk-Chattyness (Status-Polling, SSE/Events).

**Performance-Optimierungs-Checkliste:**

- Hot-Path-Allokationen vermeiden / wiederverwenden (Puffer-Pools, `reserve`, fixed buffers)
- Doppelte Serialisierung (JSON gebaut, kopiert, nochmals kopiert)
- Sync-I/O oder lange Arbeit in Event-Callbacks / hohen Prioritäten
- Polling-Intervalle zu kurz (Status, Display, Battery, Heartbeat) → CPU + Netz
- MQTT: Payload-Größe, Publish-Frequenz, Retain/Duplikate, Topic-Sturm
- Web: große Responses, fehlendes Caching/ETag, blocking Handlers
- Display: Full-Refresh statt Partial, zu hohe FPS im Idle
- Frontend: unnötige Store-Updates, fehlendes Debounce, große Dependencies im Bundle
- Flash/NVS: unnötige Writes in Loops (Wear + Latenz)
- Cross-Link zu Power: jede unnötige Wake/Netz-Aktivität ist auch Energie-Finding → an Agent 7 verweisen oder gemeinsam taggen

Pfade: `src/mqtt/`, `src/wifi/`, `src/web/`, `src/display/`, `src/network/`, `src/battery/`, `frontend/src/`.

### Agent 5 — Frontend & API-Contract

Prefix: `FE`

Fokus: Svelte State-Ownership, Error/Loading-UX, API-Contract vs Firmware-Routes (Typen, Felder, Enums), i18n-Vollständigkeit, a11y (Labels, Fokus, Keyboard), XSS (innerHTML/ungefilterte Daten), unsichere URL-Handling, Cleanup in `onDestroy`/`$effect` (EventListener, Timer/Intervals, WebSocket/SSE, Subscriptions, Object-URL/Blob-Leaks), Testbarkeit kritischer Flows (WiFi, MQTT, OTA, Settings), Mock-Parity `frontend/mock` vs echtes Gerätverhalten. Frontend und Firmware als ein System: Contract-Brüche sind High.

**Frontend-Race-/Leak-Checkliste:**

- Stale fetch / out-of-order Responses überschreiben neueren State
- Doppelte Submit / parallele Mutationen ohne Guard
- SSE/EventSource vs REST: inkonsistenter Snapshot
- Effect/Subscription-Races bei Navigation / Strict-Remount
- Timer/SSE ohne Cleanup = Leak; AbortController fehlt bei Unmount
- Security-relevant (Auth/Settings)? → `SEC`-Querverweis

Pfade: `frontend/src/`, `frontend/mock/`, `frontend/e2e/`, passende `src/web/routes/*`.

### Agent 6 — Tests, CI, Tooling, Docs/Ops

Prefix: `TEST`

Fokus: Abdeckung kritischer Pfade (MQTT reconnect, WiFi recovery, CSRF, OTA allow, NVS, **Concurrency/Race-Szenarien**, **Rate-Limit-Races**, **Power-Save-Pfade** falls testbar), Flaky/fragile Tests (oft Symptom echter Races), fehlende Negativ-Tests, CI-Workflows (was fehlt: clang-tidy, sanitizer/native ASAN/TSan falls möglich, bundle size, security scan, leak-Tests), Scripts/Flasher (Fehlerpfade, Idempotenz, unsichere Defaults), README/Docs vs Code-Drift, sichere Default-Configs, klarer Update-/Recovery-Pfad, Observability (stack monitor, task watchdog, heap-Watermarks sinnvoll genutzt?).

Pfade: `test/`, `frontend/**/*.test.ts`, `frontend/e2e/`, `.github/`, `scripts/`, `flasher/`, `docs/`, `Makefile`, `platformio.ini`.

### Agent 7 — Power Saving & Energieeffizienz

Prefix: `PWR`

**Eigener Pflicht-Agent für Stromsparen** — Batterie-/Idle-Verhalten systematisch prüfen.

**Power-Checkliste (muss abgearbeitet werden):**

- WiFi Power-Save / Modem-Sleep: aktiv? korrekt nach Connect? deaktiviert in AP/Captive ohne Grund dauerhaft?
- Unnötiges WLAN-Wecken: periodische Scans, DNS, NTP, HTTP-Polling im Idle
- MQTT Keepalive / Publish-Intervall: zu aggressiv für Battery? Idle ohne sinnvolle Drosselung?
- CPU: Busy-Loops, enge `delay(1)`-Polls, hohe Task-Frequenzen ohne Need; Light-Sleep / Tickless möglich aber ungenutzt?
- Display: Backlight immer an? Timeout? Partial vs Full Refresh; Idle-Screen sparsam?
- LED / Audio / Heart / Button: aktive Polling vs Interrupt; unnötiges Blinken im Idle
- Battery-Pfad: Sampling-Rate, ADC-Wakeups; Low-Battery → Power-Save-Escalation vorhanden?
- Webserver / SSE: offene Connections halten Radio/CPU wach; Idle-Timeouts?
- OTA / Admin-Aktivität: nach Abschluss Rückkehr in sparsamen Zustand?
- NVS/Flash-Writes im Idle (Energie + Wear)
- sdkconfig / WiFi-PS / CPU-Freq Defaults vs Docs
- Trade-offs dokumentieren: wo Latenz/UX bewusst gegen Strom getauscht wird (Info, kein False-Positive)

Jedes PWR-Finding: geschätzter Impact (Idle-mA / Radio-on-Zeit / Wakeups), nicht nur „könnte Strom sparen“.

Pfade: `src/wifi/`, `src/mqtt/`, `src/network/`, `src/display/`, `src/battery/`, `src/led/`, `src/button/`, `src/heart/`, `src/web/`, `src/async/`, `src/main.cpp`, `sdkconfig.defaults`, `platformio.ini`, relevant `docs/` (Power/Battery falls vorhanden).

## Prompt für jeden Subagent (so mitgeben)

Jeder Subagent bekommt:

- Modell: `cursor-grok-4.5-high-fast` (nur Analyse, keine Edits, keine Commits)
- Seine Kategorie, Prefix, Pfade, Fokus-Checkliste von oben
- Regeln:
  - nur belegte Findings (`pfad:zeile`)
  - Verdacht als `Suspect` markieren und sagen was zur Bestätigung nötig wäre
  - keine Style-Nits ohne Impact
  - ESP-Realität beachten (RAM, Flash, Uptime, WLAN, **Strom**)
  - Pflicht-Checklisten seines Agents vollständig abarbeiten und im Report kurz bestätigen („Checkliste: erledigt“)
- Ausgabeformat:

Jedes Finding:

- ID: `<PREFIX>-NN` (STAB/SEC/QUAL/PERF/FE/TEST/PWR)
- Severity: Critical | High | Medium | Low | Info
- Titel
- Ort: `pfad:zeile` + kurzes Snippet
- Problem
- Impact (was in Produktion/Device passieren kann — bei PWR: Energie/Wakeups; bei STAB-Race: Crash/Corruption/Lost Update + Trigger; bei SEC-Race: Exploit-Fenster; bei STAB-Leak: Wachstumsrate/Trigger)
- Empfohlener Fix (konkret, kurz)
- Aufwand: S | M | L
- Evidenzstärke: Confirmed | Suspect
- Tags (optional): `leak` | `race` | `power` | `perf` | `auth` | …

Am Ende jedes Agent-Reports zusätzlich:

- 3–7 Positives in seinem Bereich
- Top-5 Fixes in seinem Bereich
- Kurzer Hinweis: welche Pflicht-Checklisten-Punkte **ohne Finding** waren (explizit „geprüft, OK“)

## Output-Dateien (nach Aggregation schreiben)

```
docs/audit/
  00-executive-summary.md
  01-firmware-stability-memory.md
  02-security.md
  03-code-quality-architecture.md
  04-performance.md
  05-frontend-api-contract.md
  06-tests-ci-docs.md
  07-power-energy.md
  08-prioritized-roadmap.md
```

### Inhalt je Datei

- `00-executive-summary.md`: Gesamteindruck (5–10 Zeilen), Top-10 Risiken repo-weit (explizit mind. je eines aus Leak / allgemeiner Race / Security(+Race) / Perf / Power falls vorhanden), Metriken (#Critical/#High/#Medium/#Low/#Info), Verweise auf die anderen Dateien
- `01`–`07`: alle Findings der jeweiligen Kategorie in Severity-Reihenfolge (Critical → Info), plus Positives, plus „geprüft OK“-Punkte der Pflicht-Checklisten; in `01` Race-Findings klar von Leak-Findings trennen (Unterabschnitte ok)
- `08-prioritized-roadmap.md`: Quick Wins → mittelfristig → tiefgreifend; die 10 wichtigsten Fixes global in Reihenfolge; offene Fragen/Annahmen; Cluster Leak / Race (allgemein) / Security-Race / Power / Perf besonders markieren

### Markdown-Konventionen

- Jedes Finding als `### F-ID — Titel`
- Severity und Kategorie am Anfang klar markieren
- Relative Links zwischen Dateien (`./02-security.md`, `./07-power-energy.md`)
- Keine leeren Dateien; wenn ein Bereich clean ist: kurz begründen
- Sprache: Deutsch
- Nur belegte Findings; Suspects klar kennzeichnen

## Abschluss an mich

Wenn fertig: Liste der geschriebenen Dateipfade + 5–10 Zeilen Executive Summary mit den wichtigsten Risiken — dabei **Leak, Race Conditions (allgemein), Security (inkl. Security-Races), Performance und Stromsparen** jeweils kurz ansprechen (auch wenn clean).
