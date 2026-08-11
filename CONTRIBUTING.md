# Zu Chaya2MQTT beitragen

Danke für dein Interesse an Chaya2MQTT.

## Vor einer Änderung

- Für reproduzierbare Fehler die Issue-Vorlage **Fehlerbericht** verwenden.
- Für Verbesserungen und Änderungsvorschläge die Vorlage **Feature- oder Änderungsvorschlag** verwenden.
- Größere Architektur-, Protokoll- oder Hardwareänderungen vor der Umsetzung im Feature-Formular abstimmen.
- Sicherheitsprobleme nicht öffentlich melden; siehe [Security Policy](.github/SECURITY.md) und „Report a vulnerability“.
- Keine Passwörter, Tokens, privaten MQTT-Topics oder sonstigen Zugangsdaten in Issues anhängen.

## Entwicklungsumgebung

Benötigt werden PlatformIO, Node.js 22 und die in [docs/TESTING.md](docs/TESTING.md) beschriebenen Werkzeuge.

```bash
cd frontend
npm ci
cd ..
make check-pr
```

## Pull Requests

- Änderungen klein und nachvollziehbar halten.
- Neue oder geänderte Logik mit passenden Tests abdecken.
- REST-, SSE- oder MQTT-Vertragsänderungen in Implementierung, Mock und Dokumentation gemeinsam aktualisieren.
- Keine Zugangsdaten, `.env`-Dateien oder generierten Build-Artefakte committen.
- Vor dem Push `make check` vollständig erfolgreich ausführen.

Die vollständigen Qualitätsgates und manuellen Hardwareprüfungen stehen in [docs/TESTING.md](docs/TESTING.md).

## Stil

- Bestehende C++-, TypeScript- und Dokumentationskonventionen beibehalten.
- Frontend-Code mit der vorhandenen Prettier-Konfiguration formatieren.
- Öffentliche Schnittstellen und sicherheitsrelevante Designentscheidungen dokumentieren.

Mit einem Beitrag bestätigst du, dass du ihn unter der Repository-Lizenz [CC BY-NC-SA 4.0](LICENSE) veröffentlichen darfst.
