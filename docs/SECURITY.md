# Sicherheit und Threat Model

Dieses Dokument beschreibt das Sicherheitsmodell von Chaya2MQTT, bekannte Einschränkungen und empfohlene Abmilderungen. Es ist keine Zusage, dass die Firmware für sicherheitskritische Umgebungen geeignet ist.

## Annahmen

| Annahme | Beschreibung |
|---------|--------------|
| Heimnetz | Das Gerät läuft in einem vertrauenswürdigen lokalen Netzwerk. |
| Physischer Zugriff | Physischer Zugriff auf Gerät und Flash wird nicht abgewehrt. |
| MQTT-Broker | Der konfigurierte Broker verwendet TLS und ein Zertifikat einer öffentlichen CA. |
| GitHub | Releases aus `EyJunge1/chaya2mqtt` sind die vertrauenswürdige OTA-Quelle. |

Nicht vorgesehen sind der Betrieb in untrusted, öffentlichen oder gemeinsam genutzten Netzen sowie der Einsatz für sicherheitskritische Funktionen.

## Setup-Access-Point

Ohne gespeicherte WLAN-Konfiguration startet das Gerät den **offenen** SoftAP `Chaya2MQTT`. Das Display zeigt SSID und Setup-URL beziehungsweise IP-Adresse. Der AP hat kein Passwort.

Jede Person in Funkreichweite kann während des Setup-Modus:

- die Weboberfläche aufrufen,
- WLAN- und MQTT-Konfiguration ändern,
- einen Neustart oder Factory Reset auslösen.

Das Gerät sollte daher nur in einer kontrollierten Umgebung eingerichtet und nicht dauerhaft im Setup-Modus betrieben werden.

## Web-Administration

| Aspekt | Status |
|--------|--------|
| Transport | HTTP auf Port 80, keine TLS-Verschlüsselung |
| Authentifizierung | Kein Login |
| Zugriff | Teilnehmer des lokalen Netzes beziehungsweise des Setup-AP |
| CSRF | Zufälliges 128-Bit-Token für zustandsändernde POST-Anfragen |
| Request-Prüfung | Host-/Origin-Allowlist für reguläre Admin-Anfragen |
| Browser-Härtung | CSP und weitere Security-Header |

Die Weboberfläche schützt nicht vor böswilligen Teilnehmern desselben Netzes. HTTP-Inhalte und eingegebene Zugangsdaten können in einem kompromittierten oder untrusted Netz mitgelesen oder manipuliert werden. CSRF-, Host- und Origin-Prüfungen sind Defense-in-Depth, aber kein Ersatz für Authentifizierung oder TLS.

## Gespeicherte Zugangsdaten

WLAN-Passwörter, MQTT-Zugangsdaten und Konfiguration liegen in der NVS des ESP32.

| Aspekt | Status |
|--------|--------|
| Zusätzliche Verschlüsselung durch die Firmware | Keine |
| Risiko | Physisches Auslesen des Flash kann Zugangsdaten offenlegen |
| Factory Reset | Löscht die verwendeten NVS-Namespaces |

Für höhere Anforderungen sind ESP32 Flash Encryption, NVS Encryption, Secure Boot und ein physisch geschütztes Gehäuse erforderlich. Diese Funktionen werden von diesem Projekt nicht automatisch eingerichtet.

## MQTT

| Aspekt | Status |
|--------|--------|
| Transport | TLS, üblicherweise Port 8883 |
| Zertifikatsprüfung | Mozilla-CA-Bundle; Hostname wird geprüft |
| Authentifizierung | Optionaler Benutzername und optionales Passwort |
| Nachrichten | Retained Heart-Counter auf abgeleiteten Topics |

Ein kompromittierter Broker oder kompromittierte Broker-Zugangsdaten können Nachrichten mitlesen, verändern oder fälschen. Verwende getrennte, minimal berechtigte Broker-Zugangsdaten und ACLs, die nur die Topics des jeweiligen Gerätepaars erlauben.

## OTA-Updates

| Aspekt | Status |
|--------|--------|
| Quelle | GitHub Releases von `EyJunge1/chaya2mqtt` |
| Download | HTTPS mit CA- und Hostname-Prüfung |
| Übertragungsprüfung | MD5 aus demselben GitHub Release |
| Kryptografische Firmware-Signatur | Nicht implementiert |
| Rollback | ESP32-OTA-Rollback mit Health-Fenster |

MD5 erkennt Übertragungsfehler, ist aber kein kryptografischer Herkunftsnachweis. Wird das GitHub-Konto, Repository oder Release kompromittiert, kann die Firmware manipulierte Images nicht unabhängig erkennen.

Für höhere Anforderungen sind Secure Boot und separat signierte Firmware-Artefakte erforderlich.

## Factory Reset und Diagnose

Ein Factory Reset löscht die Namespaces `wifi`, `mqtt`, `cfg` und `chaya`. Er kann über zehn Sekunden Tastendruck oder über einen CSRF-geschützten Request im lokalen Netz ausgelöst werden.

Core-Dumps werden nicht über einen unauthentifizierten HTTP-Endpunkt angeboten. Sie können bei physischem Zugriff per USB ausgelesen werden und möglicherweise sensible Laufzeitdaten enthalten.

## Verantwortungsvolle Meldung

Bitte veröffentliche vermutete Schwachstellen nicht zuerst als öffentliches Issue. Nutze nach der Veröffentlichung bevorzugt GitHubs Funktion **Report a vulnerability** im Security-Bereich des Repositories.

Nenne dabei:

- betroffene Version beziehungsweise Commit,
- reproduzierbare Schritte,
- mögliche Auswirkungen,
- bekannte Voraussetzungen oder erforderlichen Zugriff,
- falls möglich einen Vorschlag zur Behebung.

Bekannte Designentscheidungen wie der offene Setup-AP, HTTP ohne Login, unverschlüsselte NVS und OTA ohne unabhängige Signatur sind in diesem Dokument beschrieben. Neue Umgehungen oder Auswirkungen dieser Grenzen sind weiterhin relevante Meldungen.

## Weitere Dokumentation

- [Web-Administration](WEB_ADMIN.md)
- [Konfiguration und NVS](CONFIGURATION.md)
- [MQTT und TLS](MQTT.md)
- [OTA und Recovery](OTA.md)
