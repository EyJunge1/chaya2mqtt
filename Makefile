# chaya2mqtt — primary local commands

ENV ?= esp32s3
PIO ?= $(HOME)/.platformio/penv/bin/pio

.PHONY: help check build upload monitor dev clean flasher

help:
	@echo "  make check    # run the full quality gate"
	@echo "  make build    # build firmware (ENV=$(ENV))"
	@echo "  make upload   # erase flash, then build and flash firmware"
	@echo "  make monitor  # open the serial monitor"
	@echo "  make dev      # start the web interface locally"
	@echo "  make flasher  # build local web-flasher site (needs RELEASES_DIR)"
	@echo "  make clean    # remove build artifacts"
	@echo ""
	@echo "Release build: make build ENV=esp32s3-release"

check:
	cd frontend && npm ci && npm run lint && npm run format:check && npm run test:coverage && npm run build
	cd flasher && npm ci && npm run lint && npm run format:check && npm run check && npm run build
	python3 scripts/embed_web_assets.py
	python3 scripts/test_embed_web_assets.py
	python3 scripts/test_patch_gxepd2_busy_wait.py
	python3 scripts/test_flasher_site.py
	"$(PIO)" test -e native
	"$(PIO)" test -e native-asan
	"$(PIO)" pkg install -g -t tool-cppcheck
	"$(PIO)" check -e esp32s3 --fail-on-defect=high -f "-<*>" -f "+<src/>"
	cd frontend && npm run test:e2e
	CHAYA_SKIP_FRONTEND_BUILD=1 "$(PIO)" run -e esp32s3-release
	python3 scripts/prepare_release_artifacts.py --build-dir .pio/build/esp32s3-release

# Example: make flasher RELEASES_DIR=/tmp/chaya-releases
flasher:
	@test -n "$(RELEASES_DIR)" || (echo "Set RELEASES_DIR=... (tag folders with firmware.factory.bin)"; exit 1)
	cd flasher && npm ci && npm run build
	python3 scripts/generate_flasher_site.py --releases-dir "$(RELEASES_DIR)"

build:
	"$(PIO)" run -e $(ENV)

upload:
	"$(PIO)" run -e $(ENV) -t erase
	"$(PIO)" run -e $(ENV) -t upload

monitor:
	"$(PIO)" run -e $(ENV) -t monitor

dev:
	cd frontend && npm run dev

clean:
	"$(PIO)" run -e $(ENV) -t clean
	$(RM) -r frontend/dist flasher/dist flasher/_site
	$(RM) src/web/assets/web_ui.bin src/web/assets/web_ui_blob.S src/web/assets/web_ui_manifest.h
