# chaya2mqtt — primary local commands

ENV ?= esp32s3
PIO ?= $(HOME)/.platformio/penv/bin/pio
FRONTEND_NPM_CI ?= npm ci
FLASHER_NPM_CI ?= npm ci
# Optional: make upload UPLOAD_PORT=/dev/cu.usbmodem101
UPLOAD_PORT_FLAG := $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)

.PHONY: help check check-frontend check-flasher check-firmware check-firmware-tests check-firmware-build upload upload-erase monitor dev clean flasher

help:
	@echo "  make check    # run the full quality gate"
	@echo "  make check-frontend  # run frontend lint, tests, build, and E2E"
	@echo "  make check-flasher   # run web-flasher checks"
	@echo "  make check-firmware  # run native/static tests and release build"
	@echo "  make upload       # build and flash debug; keep saved settings"
	@echo "  make upload-erase # erase all settings, then build and flash debug"
	@echo "  make monitor  # open the serial monitor"
	@echo "  make dev      # start the web interface locally"
	@echo "  make flasher  # build local web-flasher site (needs RELEASES_DIR)"
	@echo "  make clean    # remove build artifacts"
	@echo "  UPLOAD_PORT=/dev/cu.usbmodem101  # pin erase/upload/monitor to a port"

check: check-frontend check-flasher check-firmware

check-frontend:
	cd frontend && $(FRONTEND_NPM_CI)
	cd frontend && npm run lint && npm run format:check && npm run test:coverage && npm run build
	cd frontend && npm run test:e2e

check-flasher:
	cd flasher && $(FLASHER_NPM_CI)
	cd flasher && npm run lint && npm run format:check && npm run check && npm run build
	python3 scripts/test_flasher_site.py

check-firmware: check-firmware-tests check-firmware-build

check-firmware-tests:
	python3 scripts/test_patch_gxepd2_busy_wait.py
	"$(PIO)" test -e native
	"$(PIO)" test -e native-asan
	"$(PIO)" pkg install -g -t tool-cppcheck
	"$(PIO)" check -e esp32s3 --fail-on-defect=high -f "-<*>" -f "+<src/>"

check-firmware-build:
	CHAYA_SKIP_FRONTEND_BUILD=1 "$(PIO)" run -e esp32s3-release
	python3 scripts/test_embed_web_assets.py
	python3 scripts/prepare_release_artifacts.py --build-dir .pio/build/esp32s3-release

# Example: make flasher RELEASES_DIR=/tmp/chaya-releases
flasher:
	@test -n "$(RELEASES_DIR)" || (echo "Set RELEASES_DIR=... (tag folders with firmware.factory.bin)"; exit 1)
	cd flasher && npm ci && npm run build
	python3 scripts/generate_flasher_site.py --releases-dir "$(RELEASES_DIR)"

upload:
	"$(PIO)" run -e $(ENV) -t upload $(UPLOAD_PORT_FLAG)

upload-erase:
	"$(PIO)" run -e $(ENV) -t erase $(UPLOAD_PORT_FLAG)
	"$(PIO)" run -e $(ENV) -t upload $(UPLOAD_PORT_FLAG)

monitor:
	"$(PIO)" run -e $(ENV) -t monitor $(UPLOAD_PORT_FLAG)

dev:
	cd frontend && npm run dev

clean:
	"$(PIO)" run -e $(ENV) -t clean
	$(RM) -r frontend/dist flasher/dist flasher/_site
	$(RM) src/web/assets/web_ui.bin src/web/assets/web_ui_blob.S src/web/assets/web_ui_manifest.h
