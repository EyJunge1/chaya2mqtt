# heart-esp32 – Kurzbefehle für PlatformIO
# Standard-Umgebung: esp32dev (Debug). Release: make upload-release

ENV     ?= esp32dev
PIO     ?= $(HOME)/.platformio/penv/bin/pio
ENV_REL ?= esp32dev-release

.PHONY: help build upload clean monitor build-release upload-release

help:
	@echo "heart-esp32 – PlatformIO über Makefile"
	@echo ""
	@echo "  make build          # bauen (ENV=$(ENV))"
	@echo "  make upload         # bauen + flashen ($(ENV))"
	@echo "  make monitor        # Serial-Monitor (115200, Decoder laut platformio.ini)"
	@echo "  make clean          # Build-Artefakte für $(ENV) löschen"
	@echo ""
	@echo "  make build-release  # nur Release bauen ($(ENV_REL))"
	@echo "  make upload-release # Release flashen (ohne davor Debug zu flashen)"
	@echo ""
	@echo "Andere Umgebung: make upload ENV=esp32dev-release"

build:
	"$(PIO)" run -e $(ENV)

upload:
	"$(PIO)" run -e $(ENV) -t upload

clean:
	"$(PIO)" run -e $(ENV) -t clean

monitor:
	"$(PIO)" run -e $(ENV) -t monitor

build-release:
	"$(PIO)" run -e $(ENV_REL)

upload-release:
	"$(PIO)" run -e $(ENV_REL) -t upload
