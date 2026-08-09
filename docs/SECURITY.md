# Sicherheit & Threat Model

Dieses Dokument beschreibt das Sicherheitsmodell von Chaya2MQTT, bekannte Einschränkungen und empfohlene Abmilderungen.

## Threat Model – Annahmen

| Annahme | Beschreibung |
|---------|--------------|
| Heimnetz | Gerät läuft in einem vertrauenswürdigen lokalen Netzwerk |
| Physischer Zugriff | Nicht im Threat Model (oder bewusst akzeptiert) |
| Broker | Externer MQTT-Broker mit TLS und öffentlicher CA |
| GitHub | Vertrauenswürdige Quelle für OTA-Releases |

## NVS / Anmeldedaten

WiFi-Passwörter, MQTT-Zugangsdaten und Konfiguration werden in der **NVS** (Non-Volatile Storage) des ESP32 abgelegt.

| Aspekt | Status |
|--------|--------|
| Verschlüsselung | **Keine** zusätzliche Verschlüsselung durch die Firmware |
| Speicherformat | Klartext in NVS-Managed-Form |
| Risiko | Physischer Flash-Zugriff (JTAG, Chip-Auslesen) kann Credentials rekonstruieren |

**Betroffene Namespaces:** `wifi`, `mqtt`, `cfg`, `chaya`

### Empfohlene Abmilderungen (außerhalb der Firmware)

- **Flash-Verschlüsselung** des ESP32 aktivieren
- **NVS-Verschlüsselung** (Key in eFuses / NVS-Keys-Partition) gemäß Espressif-Dokumentation
- Gerät physisch schützen (Gehäuse, Zugangskontrolle)

## Web-Administration

| Aspekt | Status |
|--------|--------|
| Transport | **HTTP (Port 80)** – keine TLS-Verschlüsselung |
| Login | **Kein** Web-Login; Admin-UI ist im lokalen Netz für alle Teilnehmer erreichbar |
| CSRF | Token über `/api/csrf`, in allen JSON-POSTs als `csrf_token` |
| CSP | `script-src 'self'; style-src 'self'` (keine Inline-Skripte) |
| UI | React-SPA aus PROGMEM; Logik über `/api/*` + SSE |
| Risiko | Jeder Host im LAN kann die Admin-UI nutzen; Credential-Sniffing im lokalen Netz möglich |

### CSRF / Host-Schutz

Es gibt **keine** Session-Authentifizierung. Schutz gegen Cross-Site-Requests und DNS-Rebinding:

| Merkmal | Wert |
|---------|------|
| CSRF | Token über `/api/csrf`, Pflichtfeld `csrf_token` in POSTs |
| Host-/Origin | Allowlist in Middleware und SSE-`authorizeConnect` |
| CSP | Keine Inline-Skripte/Styles |

Der Setup-Access-Point **`Chaya2MQTT`** ist bewusst **offen (ohne WPA/PSK)**, damit die Ersteinrichtung ohne vorab bekanntes Passwort möglich ist. Er sollte nur im Einrichtungs- bzw. WLAN-Fallbackmodus genutzt werden.

### Öffentliche Routen

SPA-Shell (`/`, `/wifi`, …), `/assets/*`, `/api/*` und `/events` sind ohne Login erreichbar (Host-Check bleibt aktiv; POSTs brauchen CSRF).

### SSE (`/events`)

| Aspekt | Status |
|--------|--------|
| Transport | Gleicher HTTP-Origin wie Admin-UI |
| Auth | Offen (kein Session-Login) |
| Host-Check | `webRequestHostAllowed()` in `authorizeConnect` (DNS-Rebinding-Schutz) |
| Limit | Max. 6 gleichzeitige Clients |

## MQTT

| Aspekt | Status |
|--------|--------|
| Transport | **TLS** (`mqtts://`, Port 8883) |
| Zertifikatsprüfung | Mozilla-CA-Bundle |
| Authentifizierung | Optional Username/Password |
| Risiko | Broker-Kompromittierung, schwache Broker-Auth |

Credentials liegen unverschlüsselt in NVS (siehe oben).

## OTA

| Aspekt | Status |
|--------|--------|
| Download | HTTPS + TLS (CA-Bundle) |
| Integrität | MD5-Sidecar gegen Übertragungsfehler (`HTTPUpdate`) |
| Code-Signatur | **Nicht implementiert** |
| Quelle | GitHub Releases (`EyJunge1/chaya2mqtt`), Kanäle Stable/Beta |
| Risiko | Kompromittiertes GitHub-Konto oder MITM auf GitHub-Seite |

### Empfohlene Abmilderungen

- Releases nur von vertrauenswürdigen Maintainers akzeptieren
- Für höhere Sicherheit: Secure-Boot + signierte Firmware (nicht in dieser Firmware)

## Factory Reset

Knopf 10 s halten oder NVS-Wipe löscht **alle** Namespaces:
- `wifi`, `mqtt`, `cfg`, `chaya`
- RAM-Zähler werden zurückgesetzt

## FreeRTOS / Thread-Safety

| Mechanismus | Zweck |
|-------------|-------|
| Mutexe | MQTT-Config, Publish-Pfad, NVS, WiFi-API |
| Atomics | Zähler, Web-Admin-Flags |
| Queues | NetCmd, DisplayMsg (serialisierte Aktionen) |
| portMUX | Counter-Snapshots für Display |

Lock-Reihenfolge bei MQTT-Mutexen ist dokumentiert und muss eingehalten werden (Deadlock-Vermeidung).

## Zusammenfassung

| Bereich | Schutz | Empfehlung |
|---------|--------|------------|
| NVS-Credentials | Keine Verschlüsselung | Flash-Verschlüsselung optional |
| Web-Admin | HTTP, CSRF + Host-Check, kein Login | Nur im vertrauenswürdigen Netz |
| MQTT | TLS + CA-Prüfung | Starker Broker mit Auth |
| OTA | TLS + MD5 | Vertrauen in GitHub-Quelle |
| Physischer Zugriff | Kein Schutz | Gerät schützen |

Für den typischen Heimnetz-Einsatz ist das aktuelle Modell ausreichend. Für höhere Anforderungen sind die oben genannten Abmilderungen auf ESP32-Ebene (Flash-Verschlüsselung, Secure Boot) zu prüfen.

## Weitere Dokumentation

- Web-Admin: [WEB_ADMIN.md](WEB_ADMIN.md)
- NVS-Keys: [CONFIGURATION.md](CONFIGURATION.md)
- MQTT-TLS: [MQTT.md](MQTT.md)
