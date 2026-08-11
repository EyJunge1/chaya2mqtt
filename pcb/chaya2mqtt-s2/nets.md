# Logische Netze

## Versorgung

- `VBUS`: USB-C VBUS vor PTC
- `+5V`: abgesicherte USB-Versorgung
- `+3V3`: Ausgang des 1-A-LDO für ESP32-S2 und Displaylogik
- `GND`

## USB

- `USB_DP` → GPIO20
- `USB_DN` → GPIO19
- `USB_DP_C` / `USB_DN_C`: USB-C bis ESD-Schutz
- `USB_DP_E` / `USB_DN_E`: ESD-Schutz bis 22-Ω-Serienwiderstände
- CC1/CC2 jeweils 5,1 kΩ nach GND
- D+/D− über ESD und 22-Ω-Serienwiderstände

## Bedienung

- `BTN` → GPIO4, aktiv LOW, 10 kΩ Pull-up
- `LED_GATE` → GPIO5 über 100 Ω; `LED_K` über 2N7002 nach GND
- `BOOT` → GPIO0
- `EN` → Reset

## Display

- `EPD_BUSY` → GPIO6
- `EPD_RST` → GPIO7
- `EPD_CS` / `EPD_MOSI` / `EPD_SCK` → GPIO10/11/12; kein MISO
- `EPD_DC` → GPIO15
- `GDR`, `RESE`, `SW`, `FLY`, `PREVGH`, `PREVGL`, `VSH2`, `VSH1`, `VSL`,
  `VDD`, `VCOM`: Boost- und Stützkondensatornetze gemäß GDEM0154F61H Rev 1.0
- Boost: 47 µH/500 mA (NR3015), Si1308EDL, 3× MBR0530;
  HV-Kondensatoren 25 V
