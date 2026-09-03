export type Lang = "en" | "de";

const translations = {
  en: {
    language: "Language",
    lightTheme: "Use light theme",
    darkTheme: "Use dark theme",
    installFirmware: "Install firmware",
    releaseChannel: "Release channel",
    stable: "Stable",
    beta: "Beta",
    loading: "Loading…",
    unavailable: "Unavailable",
    noBeta: "No beta published",
    firmwareUnavailable: "Firmware unavailable",
    versionLoadError: "Firmware versions could not be loaded ({detail}).",
    retry: "Try again",
    loadingLabel: "Loading",
    connectAndFlash: "Flash",
    unsupported:
      "Your browser does not support Web Serial. Use Chrome, Edge, or another Chromium browser on a desktop system.",
    secureContext: "Web Serial requires HTTPS or localhost. Open the GitHub Pages address.",
    noBetaPublished: "No Beta release published",
    noStablePublished: "No Stable release published",
    selectAvailable: "Select an available channel or publish a matching GitHub Release first.",
    howItWorks: "How it works",
    connectUsb: "Connect USB-C",
    connectUsbHint: "Use a data cable. Hold BOOT only if no serial port appears.",
    selectPort: "Allow USB serial port",
    selectPortHint:
      "The browser asks for access to serial ports — choose the ESP32 on the USB cable, not a Bluetooth device.",
    configure: "Configure",
    configureBefore: "Then connect to",
    configureAfter: "and open",
    configureEnd: ".",
    documentation: "Documentation",
    portError: "Could not open the serial port. Unplug the device, plug it back in, and try again.",
    noPortTitle: "No port selected",
    noPortIntro: "If you cancelled the port picker or your device was missing, try these steps:",
    noPortStepConnected:
      "Make sure the device is connected to this computer (the one running this browser).",
    noPortStepPower: "Most devices have a tiny power LED — if yours has one, confirm it is on.",
    noPortStepCable: "Use a USB data cable, not a charge-only cable.",
    noPortStepLinux:
      "On Linux, your user must be in the dialout group to access the serial device:",
    noPortStepLinuxHint: "Log out and back in (or reboot) after changing groups.",
    noPortStepDrivers: "Install drivers for the USB-UART chip on your board:",
    noPortDriversWinMac: "Windows & Mac",
    noPortClose: "Close",
    noPortRetry: "Try again",
    "flash.confirmTitle": "Install firmware",
    "flash.confirmText": "Flash Chaya2MQTT {version} to the connected device.",
    "flash.eraseLabel": "Erase flash before install",
    "flash.eraseHint":
      "Leave unchecked for normal updates. Enable only for first install or recovery when settings are corrupt — erases firmware and all NVS data.",
    "flash.install": "Install",
    "flash.cancel": "Cancel",
    "flash.runningTitle": "Installing…",
    "flash.working": "Please wait",
    "flash.chip": "Detected: {chip}",
    "flash.doneTitle": "Install complete",
    "flash.doneText": "The device reboots automatically. Finish setup over Wi‑Fi:",
    "flash.nextUsb": "You can unplug USB if you want — Wi‑Fi setup works on battery too.",
    "flash.nextWifiBefore": "Join the SoftAP",
    "flash.nextWifiAfter": ".",
    "flash.nextOpenBefore": "Open",
    "flash.nextOpenAfter": " in the browser and configure Wi‑Fi / MQTT.",
    "flash.errorTitle": "Install failed",
    "flash.close": "Close",
    "flash.retry": "Try again",
    "flash.phase.initializing": "Initializing connection…",
    "flash.phase.initialized": "Connected ({chip})",
    "flash.phase.preparing": "Downloading firmware…",
    "flash.phase.prepared": "Firmware ready",
    "flash.phase.erasing": "Erasing flash…",
    "flash.phase.erased": "Flash erased",
    "flash.phase.writing": "Writing firmware…",
    "flash.phase.writingPct": "Writing firmware… {pct}%",
    "flash.phase.written": "Write complete",
    "flash.phase.finished": "Done",
    "flash.error.init":
      "Could not enter download mode. Hold BOOT, click Install again, then release BOOT.",
    "flash.error.unsupported": "Unsupported chip: {chip}",
    "flash.error.download": "Firmware download failed. Check your network and try again.",
    "flash.error.hashMismatch":
      "Firmware integrity check failed (SHA-256 mismatch). Do not flash — refresh the page and try again.",
    "flash.error.hashMissing":
      "Firmware checksum is missing or invalid. Do not flash — refresh the page and try again.",
    "flash.error.erase": "Erase failed. Reconnect USB and try again.",
    "flash.error.write": "Write failed. Reconnect USB and try again.",
  },
  de: {
    language: "Sprache",
    lightTheme: "Helles Design aktivieren",
    darkTheme: "Dunkles Design aktivieren",
    installFirmware: "Firmware installieren",
    releaseChannel: "Release-Kanal",
    stable: "Stable",
    beta: "Beta",
    loading: "Wird geladen …",
    unavailable: "Nicht verfügbar",
    noBeta: "Kein Beta veröffentlicht",
    firmwareUnavailable: "Firmware nicht verfügbar",
    versionLoadError: "Firmware-Versionen konnten nicht geladen werden ({detail}).",
    retry: "Erneut versuchen",
    loadingLabel: "Laden",
    connectAndFlash: "Flashen",
    unsupported:
      "Dein Browser unterstützt Web Serial nicht. Verwende Chrome, Edge oder einen anderen Chromium-Browser auf einem Desktop-System.",
    secureContext: "Web Serial benötigt HTTPS oder localhost. Öffne die GitHub-Pages-Adresse.",
    noBetaPublished: "Kein Beta-Release veröffentlicht",
    noStablePublished: "Kein Stable-Release veröffentlicht",
    selectAvailable:
      "Wähle einen verfügbaren Kanal oder veröffentliche zuerst ein passendes GitHub Release.",
    howItWorks: "So funktioniert es",
    connectUsb: "USB-C verbinden",
    connectUsbHint: "Verwende ein Datenkabel. BOOT nur halten, wenn kein Port erscheint.",
    selectPort: "USB-Seriellport erlauben",
    selectPortHint:
      "Der Browser fragt nach Zugriff auf serielle Ports — wähle den ESP32 am USB-Kabel, kein Bluetooth-Gerät.",
    configure: "Einrichten",
    configureBefore: "Danach mit",
    configureAfter: "verbinden und",
    configureEnd: " öffnen.",
    documentation: "Dokumentation",
    portError:
      "Serieller Port konnte nicht geöffnet werden. Gerät abstecken, wieder anstecken und erneut versuchen.",
    noPortTitle: "Kein Port ausgewählt",
    noPortIntro: "Wenn du die Portauswahl abgebrochen hast oder dein Gerät fehlt, prüfe Folgendes:",
    noPortStepConnected:
      "Stelle sicher, dass das Gerät an diesem Computer hängt (dem, der diesen Browser ausführt).",
    noPortStepPower:
      "Die meisten Geräte haben eine kleine Power-LED — falls vorhanden, sollte sie leuchten.",
    noPortStepCable: "Verwende ein USB-Datenkabel, kein reines Ladekabel.",
    noPortStepLinux:
      "Unter Linux muss dein Benutzer in der Gruppe dialout sein, um den seriellen Port zu nutzen:",
    noPortStepLinuxHint: "Danach ab- und wieder anmelden (oder neu starten).",
    noPortStepDrivers: "Treiber für den USB-UART-Chip deines Boards installieren:",
    noPortDriversWinMac: "Windows & Mac",
    noPortClose: "Schließen",
    noPortRetry: "Erneut versuchen",
    "flash.confirmTitle": "Firmware installieren",
    "flash.confirmText": "Chaya2MQTT {version} auf das verbundene Gerät flashen.",
    "flash.eraseLabel": "Flash vor der Installation löschen",
    "flash.eraseHint":
      "Für normale Updates ausgelassen lassen. Nur bei Erstinstallation oder Recovery bei kaputten Einstellungen — löscht Firmware und alle NVS-Daten.",
    "flash.install": "Installieren",
    "flash.cancel": "Abbrechen",
    "flash.runningTitle": "Installation läuft …",
    "flash.working": "Bitte warten",
    "flash.chip": "Erkannt: {chip}",
    "flash.doneTitle": "Installation fertig",
    "flash.doneText": "Das Gerät startet neu. Einrichtung danach per WLAN:",
    "flash.nextUsb": "USB kannst du abstecken — die WLAN-Einrichtung geht auch mit Akku.",
    "flash.nextWifiBefore": "Mit SoftAP",
    "flash.nextWifiAfter": " verbinden.",
    "flash.nextOpenBefore": "Im Browser",
    "flash.nextOpenAfter": " öffnen und WLAN / MQTT einrichten.",
    "flash.errorTitle": "Installation fehlgeschlagen",
    "flash.close": "Schließen",
    "flash.retry": "Erneut versuchen",
    "flash.phase.initializing": "Verbindung wird initialisiert …",
    "flash.phase.initialized": "Verbunden ({chip})",
    "flash.phase.preparing": "Firmware wird geladen …",
    "flash.phase.prepared": "Firmware bereit",
    "flash.phase.erasing": "Flash wird gelöscht …",
    "flash.phase.erased": "Flash gelöscht",
    "flash.phase.writing": "Firmware wird geschrieben …",
    "flash.phase.writingPct": "Firmware wird geschrieben … {pct} %",
    "flash.phase.written": "Schreiben abgeschlossen",
    "flash.phase.finished": "Fertig",
    "flash.error.init":
      "Download-Modus fehlgeschlagen. BOOT halten, erneut Installieren, dann BOOT loslassen.",
    "flash.error.unsupported": "Nicht unterstützter Chip: {chip}",
    "flash.error.download":
      "Firmware-Download fehlgeschlagen. Netzwerk prüfen und erneut versuchen.",
    "flash.error.hashMismatch":
      "Integritätsprüfung fehlgeschlagen (SHA-256 stimmt nicht). Nicht flashen — Seite neu laden und erneut versuchen.",
    "flash.error.hashMissing":
      "Firmware-Prüfsumme fehlt oder ist ungültig. Nicht flashen — Seite neu laden und erneut versuchen.",
    "flash.error.erase": "Löschen fehlgeschlagen. USB neu stecken und erneut versuchen.",
    "flash.error.write": "Schreiben fehlgeschlagen. USB neu stecken und erneut versuchen.",
  },
} as const;

export type TranslationKey = keyof (typeof translations)["en"];

export function detectLanguage(): Lang {
  try {
    const stored = localStorage.getItem("chaya2mqtt.lang");
    if (stored === "en" || stored === "de") return stored;
  } catch {
    // Fall through to browser language.
  }

  const candidates = [...(navigator.languages ?? []), navigator.language];
  for (const raw of candidates) {
    const code = raw?.toLowerCase().split("-")[0];
    if (code === "de" || code === "en") return code;
  }
  return "en";
}

export function translate(
  lang: Lang,
  key: TranslationKey,
  params: Record<string, string | number> = {},
): string {
  let text: string = translations[lang][key] ?? translations.en[key];
  for (const [name, value] of Object.entries(params)) {
    text = text.replaceAll(`{${name}}`, String(value));
  }
  return text;
}
