# Logical nets

## Power

- `VBUS`: USB-C VBUS before the PTC
- `+5V`: fused USB supply
- `+3V3`: output of the 1 A LDO for the ESP32-S2 and display logic
- `GND`

## USB

- `USB_DP` → GPIO20
- `USB_DN` → GPIO19
- `USB_DP_C` / `USB_DN_C`: USB-C to ESD protection
- `USB_DP_E` / `USB_DN_E`: ESD protection to 22 Ω series resistors
- CC1/CC2 each have 5.1 kΩ to GND
- D+/D− through ESD protection and 22 Ω series resistors

## Controls

- `BTN` → GPIO4, active LOW, 10 kΩ pull-up
- `LED_GATE` → GPIO5 through 100 Ω; `LED_K` through 2N7002 to GND
- `BOOT` → GPIO0
- `EN` → reset

## Display

- `EPD_BUSY` → GPIO6
- `EPD_RST` → GPIO7
- `EPD_CS` / `EPD_MOSI` / `EPD_SCK` → GPIO10/11/12; no MISO
- `EPD_DC` → GPIO15
- `GDR`, `RESE`, `SW`, `FLY`, `PREVGH`, `PREVGL`, `VSH2`, `VSH1`, `VSL`,
  `VDD`, `VCOM`: boost and reservoir capacitor nets according to GDEM0154F61H Rev 1.0
- Boost: 47 µH/500 mA (NR3015), Si1308EDL, 3× MBR0530;
  HV capacitors 25 V
