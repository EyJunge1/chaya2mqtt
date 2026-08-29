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

Vollständiger Audit: Codequalität, Sicherheit, Performance, Memory/Leaks, ESP-Stabilität, Frontend, Tests/CI.
Findings nur belegen (Datei + Zeile). Keine spekulativen Fixes umsetzen — nur Reports.
Keine kosmetischen Nitpicks ohne Impact. Keine Massen-Style-Nits (Formatierung), außer sie maskieren Bugs.

## Repo-Kontext

ESP32-S3 Firmware (PlatformIO/ESP-IDF), MQTT-Bridge, Admin-Web-UI (Svelte), OTA, WiFi/Captive Portal, NVS, FreeRTOS-Tasks, Display/LED/Battery/Button, Flasher, native Tests + Frontend Unit/E2E.
Bereiche: `src/`, `frontend/`, `test/`, `flasher/`, `scripts/`, `sim/`, `docs/`, `.github/`, `platformio.ini`, `partitions_*.csv`, `sdkconfig.defaults`.

ESP-Realität beachten: begrenzter RAM/Flash, lange Uptime, schlechtes WLAN, Strom.

## Vorgehen (Orchestrator)

1. Kurz selbst Repo-Orientierung (Struktur, kritische Pfade: Boot, Tasks, Netz, MQTT, Web/API, OTA, Frontend↔Firmware-Contract).
2. Sofort alle 6 Agents parallel starten mit `model: "cursor-grok-4.5-high-fast"`.
3. Auf alle Agents warten, Findings deduplizieren (gleiche Root Cause → ein Finding, Querverweis).
4. Thematische Markdown-Dateien unter `docs/audit/` schreiben (Ordner anlegen falls nötig).
5. Mir die Pfade der geschriebenen Dateien + kurze Executive Summary zurückgeben.
6. Keine Code-Änderungen außerhalb von `docs/audit/`.

## Agent-Aufteilung (genau diese 6, parallel)

### Agent 1 — Firmware Stability & Memory

Prefix: `STAB`

Fokus: FreeRTOS (Stack-Größen, Prioritäten, Blocking in ISRs, Task-Starvation, Watchdog TWDT/IWDT, Deadlocks/Priority Inversion), Heap (Fragmentierung, große Stack-Allokationen, String-Bloat, Alloc/Free-Schleifen), Leak-Pfade bei Reconnect/OTA/Scan/lange Uptime, NVS (Keys, Größenlimits, Write-Wear, korrupte Defaults, Race beim Speichern), WiFi (Reconnect-Stürme, Scan vs Connect, AP/STA, Captive Portal, Power-Save), MQTT (Session/Reconnect, QoS, Topic/Payload-Größe, Backoff), OTA (parallele Writes, Health nach Update, Rollback), Webserver (Blocking Handler, große Responses im RAM), Display/Audio/LED/Button (Timing, Busy-Loops, Mutex, ISR), Timing (`delay` vs Tick, Busy-Wait, Drift), Brownout/Low-Battery, Boot-Loops, Soft-Reset, Partitionstabelle/Flash/Binary-Size, fehlende Deregistrierung von Event-Handlern/Sockets/Task-Handles/Queues.

Pfade: `src/async/`, `src/diag/`, `src/wifi/`, `src/mqtt/`, `src/ota/`, `src/config/`, `src/display/`, `src/battery/`, `src/led/`, `src/button/`, `src/heart/`, `src/network/`, `src/main.cpp`, `partitions_*.csv`, `sdkconfig.defaults`.

### Agent 2 — Security

Prefix: `SEC`

Fokus: Auth/Session für Admin-API, CSRF (Token-Bindung, SameSite/Origin/Host-Checks), Injection (MQTT-Topics/Payloads, JSON, Query/Path, Host-Header, Open Redirect), Secrets in Repo/Logs/Frontend-Bundle (Passwörter, Tokens, WiFi, MQTT Creds), TLS (Zertifikatsprüfung, insecure Defaults), OTA (untrusted URL, Allowlist, Signatur/Integrität, partial update, Downgrade), XSS/CSRF im SPA + API, Path Traversal bei Assets, Rate-Limits/Brute-Force auf Login & sensible Endpoints, Least Privilege in CI/Scripts/Flasher.

Pfade: `src/web/` (csrf, host_validate, middleware, routes, OTA-APIs), `src/ota/`, `src/tls/`, `src/mqtt/`, `src/wifi/`, `frontend/src/api/`, `scripts/`, `flasher/`, `.github/`.

### Agent 3 — Firmware Quality & Architecture

Prefix: `QUAL`

Fokus: Duplikation, tote Pfade, inkonsistente Error-Handling-Patterns, Modulgrenzen (wifi/mqtt/web/ota/config), zyklische Dependencies, Naming, Konstanten vs Magic Numbers, Header-Hygiene, C++ Ownership/Lebensdauer/UB/Buffer-Overflows/Integer-Overflow/unchecked snprintf, Logging (PII/Secrets, Spam, Levels), Config-Drift zwischen `app_config`, NVS-Keys, Frontend-Types, Docs.

Pfade: gesamtes `src/`, relevant `docs/` nur für Drift zu Code.

### Agent 4 — Performance

Prefix: `PERF`

Fokus: Hot Paths (MQTT Publish, WiFi Events, Web JSON, Display Refresh), unnötige Kopien, große Temporaries, JSON-Parsing-Kosten, Polling vs Event-driven, zu aggressives Reconnect/Publish, Frontend unnötige Re-Renders, große Bundles, fehlendes Code-Splitting, schwere Sync-Arbeit im UI-Thread, Netzwerk-Chattyness (Status-Polling, SSE/Events).

Pfade: `src/mqtt/`, `src/wifi/`, `src/web/`, `src/display/`, `src/network/`, `frontend/src/`.

### Agent 5 — Frontend & API-Contract

Prefix: `FE`

Fokus: Svelte State-Ownership, Race bei API-Calls, Error/Loading-UX, API-Contract vs Firmware-Routes (Typen, Felder, Enums), i18n-Vollständigkeit, a11y (Labels, Fokus, Keyboard), XSS (innerHTML/ungefilterte Daten), unsichere URL-Handling, Cleanup in `onDestroy`/`$effect` (EventListener, Timer/Intervals, WebSocket/SSE, Subscriptions, Object-URL/Blob-Leaks), Testbarkeit kritischer Flows (WiFi, MQTT, OTA, Settings), Mock-Parity `frontend/mock` vs echtes Gerätverhalten. Frontend und Firmware als ein System: Contract-Brüche sind High.

Pfade: `frontend/src/`, `frontend/mock/`, `frontend/e2e/`, passende `src/web/routes/*`.

### Agent 6 — Tests, CI, Tooling, Docs/Ops

Prefix: `TEST`

Fokus: Abdeckung kritischer Pfade (MQTT reconnect, WiFi recovery, CSRF, OTA allow, NVS), Flaky/fragile Tests, fehlende Negativ-Tests, CI-Workflows (was fehlt: clang-tidy, sanitizer/native ASAN falls möglich, bundle size, security scan), Scripts/Flasher (Fehlerpfade, Idempotenz, unsichere Defaults), README/Docs vs Code-Drift, sichere Default-Configs, klarer Update-/Recovery-Pfad, Observability (stack monitor, task watchdog sinnvoll genutzt?).

Pfade: `test/`, `frontend/**/*.test.ts`, `frontend/e2e/`, `.github/`, `scripts/`, `flasher/`, `docs/`, `Makefile`, `platformio.ini`.

## Prompt für jeden Subagent (so mitgeben)

Jeder Subagent bekommt:

- Modell: `cursor-grok-4.5-high-fast` (nur Analyse, keine Edits, keine Commits)
- Seine Kategorie, Prefix, Pfade, Fokus-Checkliste von oben
- Regeln: nur belegte Findings; Verdacht als `Suspect` markieren und sagen was zur Bestätigung nötig wäre; keine Style-Nits ohne Impact; ESP-Realität beachten
- Ausgabeformat:

Jedes Finding:

- ID: `<PREFIX>-NN` (STAB/SEC/QUAL/PERF/FE/TEST)
- Severity: Critical | High | Medium | Low | Info
- Titel
- Ort: `pfad:zeile` + kurzes Snippet
- Problem
- Impact (was in Produktion/Device passieren kann)
- Empfohlener Fix (konkret, kurz)
- Aufwand: S | M | L
- Evidenzstärke: Confirmed | Suspect

Am Ende jedes Agent-Reports zusätzlich:

- 3–7 Positives in seinem Bereich
- Top-5 Fixes in seinem Bereich

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
  07-prioritized-roadmap.md
```

### Inhalt je Datei

- `00-executive-summary.md`: Gesamteindruck (5–10 Zeilen), Top-10 Risiken repo-weit, Metriken (#Critical/#High/#Medium/#Low/#Info), Verweise auf die anderen Dateien
- `01`–`06`: alle Findings der jeweiligen Kategorie in Severity-Reihenfolge (Critical → Info), plus Positives
- `07-prioritized-roadmap.md`: Quick Wins → mittelfristig → tiefgreifend; die 10 wichtigsten Fixes global in Reihenfolge; offene Fragen/Annahmen

### Markdown-Konventionen

- Jedes Finding als `### F-ID — Titel`
- Severity und Kategorie am Anfang klar markieren
- Relative Links zwischen Dateien (`./02-security.md`)
- Keine leeren Dateien; wenn ein Bereich clean ist: kurz begründen
- Sprache: Deutsch
- Nur belegte Findings; Suspects klar kennzeichnen

## Abschluss an mich

Wenn fertig: Liste der geschriebenen Dateipfade + 5–10 Zeilen Executive Summary mit den wichtigsten Risiken.
