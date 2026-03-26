# PlatformIO liegt typischerweise hier; so funktioniert `make` auch ohne `pio` im PATH.
PIO ?= $(HOME)/.platformio/penv/bin/pio

.PHONY: all build upload monitor clean

all: build

build:
	"$(PIO)" run

upload:
	"$(PIO)" run -t upload

monitor:
	"$(PIO)" device monitor

clean:
	"$(PIO)" run -t clean
