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
| Session | Cookie `chaya_sid` (HttpOnly, SameSite=Strict) |
| CSRF | Token in allen POST-Formularen |
| Risiko | Session-Hijacking / Credential-Sniffing im lokalen Netz möglich |

### Web-Auth (optional)

Wenn aktiviert (`cfg/authEn`):

| Merkmal | Wert |
|---------|------|
| Challenge | 6-stelliger Code auf E-Ink |
| Bestätigung | Physischer Tastendruck erforderlich |
| Code-Gültigkeit | 5 Minuten |
| Lockout | 1 Stunde nach 3 Fehlversuchen |
| Session | 24 Stunden |
| Parallele Sessions | **Eine** globale Session pro Gerät (neuer Login invalidiert ältere Cookies) |

**Stärke:** Remote-Angreifer ohne physischen Gerätezugriff kann sich nicht authentifizieren (Tastendruck nötig).

**Schwäche:** Im AP-Modus (Ersteinrichtung) ist Auth deaktiviert – jeder im AP-Bereich kann konfigurieren.

### Öffentliche Routen (ohne Auth)

Im AP-Modus: `/`, `/wifi*`, `/favicon.ico`

Im STA-Modus (Auth aktiv): `/`, `/wifi`, `/wifi-connect*`, `/auth`, `/logout`, `/favicon.ico`
(`/wifi-scan` und `/wifi-status` erfordern im STA-Modus eine aktive Session)

### SSE (`/events`)

| Aspekt | Status |
|--------|--------|
| Transport | Gleicher HTTP-Origin wie Admin-UI |
| Auth | AP: offen; STA ohne Web-Auth: offen; STA mit Web-Auth: Session-Cookie wie HTML-Routen |
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
| Integrität | SHA256-Hash-Verifikation |
| Code-Signatur | **Nicht implementiert** |
| Quelle | GitHub Releases (`EyJunge1/chaya2mqtt`) |
| Risiko | Kompromittiertes GitHub-Konto oder MITM auf GitHub-Seite |

### Empfohlene Abmilderungen

- Releases nur von vertrauenswürdigen Maintainers akzeptieren
- SHA256-Hash manuell gegen Release-Notes prüfen
- Für höhere Sicherheit: Secure-Boot + signierte Firmware (nicht in dieser Firmware)

## Factory Reset

Knopf 10 s halten oder NVS-Wipe löscht **alle** Namespaces:
- `wifi`, `mqtt`, `cfg`, `chaya`
- Session-Cookie wird invalidiert
- RAM-Zähler werden zurückgesetzt

## FreeRTOS / Thread-Safety

| Mechanismus | Zweck |
|-------------|-------|
| Mutexe | MQTT-Config, Publish-Pfad, NVS, WiFi-API |
| Atomics | Zähler, Web-Admin-Flags, Auth-State |
| Queues | NetCmd, DisplayMsg (serialisierte Aktionen) |
| portMUX | Counter-Snapshots für Display |

Lock-Reihenfolge bei MQTT-Mutexen ist dokumentiert und muss eingehalten werden (Deadlock-Vermeidung).

## Zusammenfassung

| Bereich | Schutz | Empfehlung |
|---------|--------|------------|
| NVS-Credentials | Keine Verschlüsselung | Flash-Verschlüsselung optional |
| Web-Admin | HTTP, optional Auth | Nur im vertrauenswürdigen Netz |
| MQTT | TLS + CA-Prüfung | Starker Broker mit Auth |
| OTA | TLS + SHA256 | Vertrauen in GitHub-Quelle |
| Physischer Zugriff | Kein Schutz | Gerät schützen |

Für den typischen Heimnetz-Einsatz ist das aktuelle Modell ausreichend. Für höhere Anforderungen sind die oben genannten Abmilderungen auf ESP32-Ebene (Flash-Verschlüsselung, Secure Boot) zu prüfen.

## Weitere Dokumentation

- Web-Auth-Flow: [WEB_ADMIN.md](WEB_ADMIN.md)
- NVS-Keys: [CONFIGURATION.md](CONFIGURATION.md)
- MQTT-TLS: [MQTT.md](MQTT.md)
