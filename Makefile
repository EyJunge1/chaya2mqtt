# chaya2mqtt – Kurzbefehle für PlatformIO und lokale Tests
# Standard-Umgebung: esp32dev (Debug). Release: make upload-release

ENV     ?= esp32dev
PIO     ?= $(HOME)/.platformio/penv/bin/pio
ENV_REL ?= esp32dev-release

.PHONY: help check check-quick check-pr check-static build upload upload-clean clean erase monitor compiledb \
	build-release upload-release erase-release upload-release-clean \
	frontend frontend-test \
	test-frontend test-coverage test-native test-asan device-sim test-e2e test-e2e-smoke \
	analyze-coredump

help:
	@echo "chaya2mqtt – PlatformIO und Tests über Makefile"
	@echo ""
	@echo "  make check            # voller hardwarefreier Gate (vor Commit)"
	@echo "  make check-quick      # Lint/Vitest/native ohne Coverage/E2E/Firmware"
	@echo "  make check-pr         # PR-Gate: quick + ASan + cppcheck"
	@echo "  make check-static     # cppcheck (esp32dev)"
	@echo "  make test-frontend    # Vitest"
	@echo "  make test-coverage    # Vitest + Coverage-Schwellen"
	@echo "  make test-native      # Unity native (inkl. Device-Simulator)"
	@echo "  make test-asan        # Unity + ASan/UBSan"
	@echo "  make device-sim       # nur Device-Simulator-Szenarien"
	@echo "  make test-e2e         # Playwright gegen Mock"
	@echo "  make test-e2e-smoke   # Playwright @smoke"
	@echo "  make analyze-coredump DUMP=…  # Core-Dump gegen firmware.elf analysieren"
	@echo ""
	@echo "  make build            # Frontend + Firmware bauen (ENV=$(ENV))"
	@echo "  make upload           # bauen + flashen ($(ENV))"
	@echo "  make erase            # Flash komplett löschen ($(ENV))"
	@echo "  make upload-clean     # erase + upload ($(ENV))"
	@echo "  make monitor          # Serial-Monitor (115200, Decoder laut platformio.ini)"
	@echo "  make clean            # Build-Artefakte für $(ENV) löschen"
	@echo "  make compiledb        # compile_commands.json neu erzeugen (clangd, $(ENV_REL))"
	@echo "  make frontend         # nur React-UI bauen + Asset-Blob einbetten"
	@echo "  make frontend-test    # Alias für test-frontend"
	@echo ""
	@echo "  make build-release    # nur Release bauen ($(ENV_REL))"
	@echo "  make upload-release   # Release flashen (ohne davor Debug zu flashen)"
	@echo "  make upload-release-clean # Flash komplett löschen + Release flashen"
	@echo ""
	@echo "Andere Umgebung: make upload ENV=esp32dev-release"
	@echo "GUI lokal ohne Flash: cd frontend && npm run dev"
	@echo "Docs: docs/TESTING.md"

check-quick:
	cd frontend && npm ci && npm run lint && npm run format:check && npm test
	"$(PIO)" test -e native

check-static:
	"$(PIO)" pkg install -g -t tool-cppcheck
	"$(PIO)" check -e esp32dev --fail-on-defect=high -f "-<*>" -f "+<src/>"

check-pr: check-quick test-asan check-static

check:
	cd frontend && npm ci && npm run lint && npm run format:check && npm run test:coverage && npm run build
	python3 scripts/embed_web_assets.py
	python3 scripts/test_embed_web_assets.py
	"$(PIO)" test -e native
	"$(PIO)" test -e native-asan
	$(MAKE) check-static
	cd frontend && npm run test:e2e
	CHAYA_SKIP_FRONTEND_BUILD=1 "$(PIO)" run -e $(ENV_REL)

test-frontend frontend-test:
	cd frontend && npm test

test-coverage:
	cd frontend && npm run test:coverage

test-native:
	"$(PIO)" test -e native

test-asan:
	"$(PIO)" test -e native-asan

device-sim:
	"$(PIO)" test -e device-sim

test-e2e:
	cd frontend && npm run test:e2e

test-e2e-smoke:
	cd frontend && npm run test:e2e:smoke

# DUMP=/path/to/coredump.bin ENV_REL=esp32dev-release
analyze-coredump:
	@test -n "$(DUMP)" || (echo "DUMP=/path/to/coredump.bin fehlt" >&2; exit 1)
	python3 scripts/analyze_coredump.py "$(DUMP)" "$(ENV_REL)"

frontend:
	cd frontend && npm ci && npm run build
	python3 scripts/embed_web_assets.py

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
	"$(PIO)" run -e $(ENV_REL) -t compiledb

monitor:
	"$(PIO)" run -e $(ENV) -t monitor

build-release:
	"$(PIO)" run -e $(ENV_REL)

upload-release:
	"$(PIO)" run -e $(ENV_REL) -t upload

erase-release:
	"$(PIO)" run -e $(ENV_REL) -t erase

upload-release-clean: erase-release upload-release
