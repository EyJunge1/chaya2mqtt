# ESP32 E-Paper Display Projekt (C++)

## 🎯 **Überblick**
Dieses Projekt verwendet **C++** (Arduino Framework) für den ESP32 mit einem Waveshare 1.54" Black/White/Red E-Paper Display.

## 🔧 **Technische Details**
- **Sprache**: C++ (Arduino Framework)
- **Board**: ESP32
- **Display**: Waveshare 1.54" E-Paper (GDEW0154Z04)
- **Build System**: PlatformIO

## 📁 **Projektstruktur**
```
esp32-heart/
├── platformio.ini          # PlatformIO Konfiguration
├── src/
│   └── main.cpp            # Hauptprogramm in C++
└── README_C++.md           # Diese Datei
```

## 🚀 **Installation & Build**

### 1. PlatformIO installieren
```bash
pip install platformio
```

### 2. Projekt bauen
```bash
platformio run
```

### 3. Auf ESP32 flashen
```bash
platformio run --target upload
```

### 4. Serial Monitor öffnen
```bash
platformio device monitor
```

## 🔌 **Pin-Belegung (nach Waveshare Datenblatt)**
- **SCK**: GPIO 13
- **MOSI**: GPIO 14  
- **CS**: GPIO 15
- **DC**: GPIO 27
- **RST**: GPIO 26
- **BUSY**: GPIO 25

## 📊 **Display-Kommandos**
Alle Display-Kommandos stammen direkt aus dem **offiziellen Waveshare C-Code**:
- `EPD_1in54b.c` - Offizieller Treiber
- Vollständige LUT-Daten implementiert
- Exakte Timing-Werte

## 🧪 **Test-Programm**
Das Programm macht folgendes:
1. **Display initialisieren** mit offiziellen Parametern
2. **Display löschen** (Clear-Funktion)
3. **5 Sekunden warten**
4. **Nochmal löschen**
5. **10 Sekunden warten**
6. **Von vorne beginnen**

## 🔍 **Debug-Ausgaben**
Alle Schritte werden über den Serial Monitor ausgegeben:
- Pin-Konfiguration
- Display-Initialisierung
- LUT-Setup
- Clear-Operationen

## 📚 **Quellen**
- **Offizieller Waveshare Code**: `/Users/arneleder/Downloads/E-Paper_code/`
- **EPD_1in54b.c**: Vollständiger C-Treiber
- **Pin-Belegung**: Waveshare Datenblatt

## 🎯 **Nächste Schritte**
1. **Display funktioniert** → Herz zeichnen
2. **MQTT integrieren** → Zähler synchronisieren
3. **Button-Handler** → Lokale Zähler-Erhöhung
