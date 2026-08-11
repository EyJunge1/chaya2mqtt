# chaya2mqtt — primary local commands

ENV ?= esp32dev
PIO ?= $(HOME)/.platformio/penv/bin/pio

.PHONY: help check build upload monitor dev clean

help:
	@echo "  make check    # run the full quality gate"
	@echo "  make build    # build firmware (ENV=$(ENV))"
	@echo "  make upload   # build and flash firmware"
	@echo "  make monitor  # open the serial monitor"
	@echo "  make dev      # start the web interface locally"
	@echo "  make clean    # remove build artifacts"
	@echo ""
	@echo "Release build: make build ENV=esp32dev-release"

check:
	cd frontend && npm ci && npm run lint && npm run format:check && npm run test:coverage && npm run build
	python3 scripts/embed_web_assets.py
	python3 scripts/test_embed_web_assets.py
	"$(PIO)" test -e native
	"$(PIO)" test -e native-asan
	"$(PIO)" pkg install -g -t tool-cppcheck
	"$(PIO)" check -e esp32dev --fail-on-defect=high -f "-<*>" -f "+<src/>"
	cd frontend && npm run test:e2e
	CHAYA_SKIP_FRONTEND_BUILD=1 "$(PIO)" run -e esp32dev-release

build:
	"$(PIO)" run -e $(ENV)

upload:
	"$(PIO)" run -e $(ENV) -t upload

monitor:
	"$(PIO)" run -e $(ENV) -t monitor

dev:
	cd frontend && npm run dev

clean:
	"$(PIO)" run -e $(ENV) -t clean
