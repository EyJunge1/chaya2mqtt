# chaya2mqtt – Kurzbefehle für PlatformIO
# Standard-Umgebung: esp32dev (Debug). Release: make upload-release

ENV     ?= esp32dev
PIO     ?= $(HOME)/.platformio/penv/bin/pio
ENV_REL ?= esp32dev-release

.PHONY: help check build upload upload-clean clean erase monitor compiledb build-release upload-release erase-release upload-release-clean frontend frontend-test

help:
	@echo "chaya2mqtt – PlatformIO über Makefile"
	@echo ""
	@echo "  make check          # Lint, Tests, Frontend- und Firmware-Build (vor Commit)"
	@echo "  make build          # Frontend + Firmware bauen (ENV=$(ENV))"
	@echo "  make upload         # bauen + flashen ($(ENV))"
	@echo "  make erase          # Flash komplett löschen ($(ENV))"
	@echo "  make upload-clean   # erase + upload ($(ENV))"
	@echo "  make monitor        # Serial-Monitor (115200, Decoder laut platformio.ini)"
	@echo "  make clean          # Build-Artefakte für $(ENV) löschen"
	@echo "  make compiledb      # compile_commands.json neu erzeugen (clangd/IDE)"
	@echo "  make frontend       # nur React-UI bauen + PROGMEM einbetten"
	@echo "  make frontend-test  # Frontend Unit-Tests"
	@echo ""
	@echo "  make build-release  # nur Release bauen ($(ENV_REL))"
	@echo "  make upload-release # Release flashen (ohne davor Debug zu flashen)"
	@echo "  make upload-release-clean # Flash komplett löschen + Release flashen"
	@echo ""
	@echo "Andere Umgebung: make upload ENV=esp32dev-release"
	@echo "GUI lokal ohne Flash: cd frontend && npm run dev"

check:
	cd frontend && npm ci && npm run lint && npm test && npm run build
	python3 tools/embed_web_assets.py
	"$(PIO)" test -e native
	"$(PIO)" run -e $(ENV_REL)

frontend:
	cd frontend && npm ci && npm run build
	python3 tools/embed_web_assets.py

frontend-test:
	cd frontend && npm test

build:
	"$(PIO)" run -e $(ENV)

upload:
	"$(PIO)" run -e $(ENV) -t upload

clean:
	"$(PIO)" run -e $(ENV) -t clean

erase:
	"$(PIO)" run -e $(ENV) -t erase

upload-erase: erase upload

upload-clean: clean upload

compiledb:
	"$(PIO)" run -e $(ENV) -t compiledb

monitor:
	"$(PIO)" run -e $(ENV) -t monitor

build-release:
	"$(PIO)" run -e $(ENV_REL)

upload-release:
	"$(PIO)" run -e $(ENV_REL) -t upload

erase-release:
	"$(PIO)" run -e $(ENV_REL) -t erase

upload-release-clean: erase-release upload-release
