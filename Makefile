# chaya2mqtt — primary local commands (production = Rust / esp-hal)

FRONTEND_NPM_CI ?= npm ci
FLASHER_NPM_CI ?= npm ci
# Optional: make upload UPLOAD_PORT=/dev/cu.usbmodem101
UPLOAD_PORT ?=
UPLOAD_PORT_FLAG := $(if $(UPLOAD_PORT),--port $(UPLOAD_PORT),)
PARTITIONS := partitions_chaya_8mb.csv
RUST_ELF := rust/target/xtensa-esp32s3-none-elf/release/chaya-fw
RUST_OUT := out/rust-release

.PHONY: help check check-frontend check-flasher check-firmware check-firmware-tests check-firmware-build \
	upload upload-erase monitor \
	dev clean flasher rust-build

help:
	@echo "  make check           # full quality gate"
	@echo "  make check-frontend  # frontend lint/tests/build/E2E"
	@echo "  make check-flasher   # web-flasher checks"
	@echo "  make check-firmware  # Rust host tests + release images"
	@echo "  make upload          # build + flash Rust release (keep NVS)"
	@echo "  make upload-erase    # erase flash, then Rust release flash"
	@echo "  make monitor         # serial monitor (espflash)"
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

dev:
	cd frontend && npm run dev

clean:
	$(RM) -r frontend/dist flasher/dist flasher/_site $(RUST_OUT)
	$(RM) -r rust/target
