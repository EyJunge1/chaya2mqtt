# chaya2mqtt — primary local commands (production = Rust / esp-hal)

ENV ?= esp32s3
PIO ?= $(HOME)/.platformio/penv/bin/pio
FRONTEND_NPM_CI ?= npm ci
FLASHER_NPM_CI ?= npm ci
# Optional: make upload UPLOAD_PORT=/dev/cu.usbmodem101
UPLOAD_PORT ?=
UPLOAD_PORT_FLAG := $(if $(UPLOAD_PORT),--port $(UPLOAD_PORT),)
PARTITIONS := partitions_chaya_8mb.csv
RUST_ELF := rust/target/xtensa-esp32s3-none-elf/release/chaya-fw
RUST_OUT := out/rust-release

.PHONY: help check check-frontend check-flasher check-firmware check-firmware-tests check-firmware-build \
	check-firmware-cpp upload upload-erase monitor monitor-cpp upload-cpp upload-erase-cpp \
	dev clean flasher rust-build

help:
	@echo "  make check           # full quality gate"
	@echo "  make check-frontend  # frontend lint/tests/build/E2E"
	@echo "  make check-flasher   # web-flasher checks"
	@echo "  make check-firmware  # Rust host tests + release images"
	@echo "  make upload          # build + flash Rust release (keep NVS)"
	@echo "  make upload-erase    # erase flash, then Rust release flash"
	@echo "  make monitor         # serial monitor (espflash)"
	@echo "  make upload-cpp      # legacy PlatformIO flash"
	@echo "  make dev             # frontend Vite"
	@echo "  make flasher         # local web-flasher site (needs RELEASES_DIR)"
	@echo "  make clean           # remove build artifacts"
	@echo "  UPLOAD_PORT=/dev/cu.usbmodem101  # pin erase/upload/monitor"

check: check-frontend check-flasher check-firmware

check-frontend:
	cd frontend && $(FRONTEND_NPM_CI)
	cd frontend && npm run lint && npm run format:check && npm run check && npm run test:coverage && npm run build
	cd frontend && npm run test:e2e

check-flasher:
	cd flasher && $(FLASHER_NPM_CI)
	cd flasher && npm run lint && npm run format:check && npm run check && npm run test && npm run build
	cd flasher && npm audit --audit-level=high
	python3 scripts/test_flasher_site.py

check-firmware: check-firmware-tests check-firmware-build

check-firmware-tests:
	cd rust && cargo test -p chaya-pure -p chaya-fw

check-firmware-build:
	bash scripts/build_rust_release.sh
	python3 scripts/check_firmware_size.py

# Legacy C++ gate (optional)
check-firmware-cpp:
	"$(PIO)" test -e native
	"$(PIO)" test -e native-asan
	"$(PIO)" pkg install -g -t tool-cppcheck
	"$(PIO)" check -e esp32s3 --fail-on-defect=high -f "-<*>" -f "+<src/>"
	bash scripts/check_clang_tidy.sh
	bash scripts/check_firmware_format.sh
	python3 scripts/test_check_firmware_size.py
	CHAYA_SKIP_FRONTEND_BUILD=1 "$(PIO)" run -e esp32s3-release
	python3 scripts/test_embed_web_assets.py
	python3 scripts/prepare_release_artifacts.py --build-dir .pio/build/esp32s3-release

rust-build:
	bash scripts/build_rust_release.sh

# Example: make flasher RELEASES_DIR=/tmp/chaya-releases
flasher:
	@test -n "$(RELEASES_DIR)" || (echo "Set RELEASES_DIR=... (tag folders with firmware.factory.bin)"; exit 1)
	cd flasher && npm ci && npm run build
	python3 scripts/generate_flasher_site.py --releases-dir "$(RELEASES_DIR)"

upload: rust-build
	espflash flash --chip esp32s3 --flash-size 8mb \
		--partition-table $(PARTITIONS) \
		$(UPLOAD_PORT_FLAG) \
		$(RUST_ELF)

upload-erase:
	espflash erase-flash $(UPLOAD_PORT_FLAG)
	$(MAKE) upload UPLOAD_PORT="$(UPLOAD_PORT)"

monitor:
	espflash monitor $(UPLOAD_PORT_FLAG)

upload-cpp:
	"$(PIO)" run -e $(ENV) -t upload $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)

upload-erase-cpp:
	"$(PIO)" run -e $(ENV) -t erase $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)
	"$(PIO)" run -e $(ENV) -t upload $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)

monitor-cpp:
	"$(PIO)" run -e $(ENV) -t monitor $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)

dev:
	cd frontend && npm run dev

clean:
	"$(PIO)" run -e $(ENV) -t clean || true
	$(RM) -r frontend/dist flasher/dist flasher/_site $(RUST_OUT)
	$(RM) -r rust/target
	$(RM) src/web/assets/web_ui.bin src/web/assets/web_ui_blob.S src/web/assets/web_ui_manifest.h
