# Sicherheit & Threat Model (Chaya2MQTT)

## NVS / Anmeldedaten

Wi‑Fi-Passwörter und MQTT-Zugangsdaten werden in der **NVS** (Non-Volatile Storage) des ESP32 abgelegt. Die Anwendung **verschlüsselt diese Werte nicht zusätzlich** — sie liegen im Klartext bzw. in der von NVS verwalteten Form. Für Angreifer mit **physischem Zugriff** auf den Flash (Auslesen des Chips, JTAG, kompromittierte Firmware) können diese Daten damit grundsätzlich rekonstruierbar sein.

**Abmilderung (optional, außerhalb dieser Firmware):**

- **Flash-Verschlüsselung** des ESP32 aktivieren
- **NVS-Verschlüsselung** (Key in eFuses / NVS-Keys-Partition) gemäß Espressif-Dokumentation

**Akzeptanz:** Wenn das Gerät nur im vertrauenswürdigen Heimnetz betrieben wird und physische Manipulation nicht im Threat Model steht, ist das aktuelle Verhalten für viele Projekte ausreichend.

## Web-Administration

Die Weboberfläche läuft standardmäßig über **HTTP (Port 80)**. Session-Cookies und Formulare sind im lokalen Netz **nicht transportverschlüsselt**. Web-Auth wird nach Bedarf per sechsstelliger Code (physisch am Display) freigeschaltet — siehe Hauptdokumentation.

## OTA

Firmware-Updates prüfen Integrität (z. B. Hash über HTTPS). Eine **Code-Signatur** der Update-Blobs ist in dieser Firmware nicht vorgesehen; das Threat Model sollte bewusst gewählt werden (z. B. Vertrauen in die GitHub-Release-Quelle).
