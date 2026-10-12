#!/usr/bin/env bash
# Build Rust device firmware and write release images under out/rust-release/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${RUST_RELEASE_OUT:-$ROOT/out/rust-release}"
PARTITIONS="${PARTITIONS:-$ROOT/partitions_chaya_8mb.csv}"
ELF="$ROOT/target/xtensa-esp32s3-none-elf/release/chaya-fw"

# espup export (optional local)
if [[ -f "${HOME}/export-esp.sh" ]]; then
  # shellcheck source=/dev/null
  source "${HOME}/export-esp.sh"
fi

# Prefer espup's xtensa gcc on PATH when present
ESP_GCC="$(ls -d "$HOME"/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin 2>/dev/null | sort | tail -1 || true)"
if [[ -n "${ESP_GCC}" ]]; then
  export PATH="${ESP_GCC}:$PATH"
fi

command -v cargo >/dev/null
command -v espflash >/dev/null

# Keep device partition table in sync with production CSV
cp "$PARTITIONS" "$ROOT/partitions.csv"

(
  cd "$ROOT"
  cargo +esp build \
    --features device \
    --release \
    --target xtensa-esp32s3-none-elf \
    -Z build-std=core,alloc
)

test -f "$ELF"

mkdir -p "$OUT"
espflash save-image \
  --chip esp32s3 \
  --flash-size 8mb \
  --partition-table "$PARTITIONS" \
  "$ELF" \
  "$OUT/firmware.bin"

espflash save-image \
  --chip esp32s3 \
  --flash-size 8mb \
  --merge \
  --partition-table "$PARTITIONS" \
  "$ELF" \
  "$OUT/firmware.factory.bin"

cp "$ELF" "$OUT/firmware.elf"

python3 "$ROOT/scripts/prepare_release_artifacts.py" \
  --build-dir "$OUT" \
  --partition-table "$PARTITIONS"

echo "Rust release artifacts in $OUT"
ls -la "$OUT"
