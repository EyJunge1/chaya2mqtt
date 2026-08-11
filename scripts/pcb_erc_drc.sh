#!/usr/bin/env bash
# Hardware gate: KiCad ERC/DRC for chaya2mqtt PCB projects.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KICAD_CLI="${KICAD_CLI:-}"

find_kicad_cli() {
  if command -v kicad-cli >/dev/null 2>&1; then
    command -v kicad-cli
    return
  fi
  local candidates=(
    "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
    "/tmp/kicad-mnt/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -x "$c" ]]; then
      printf '%s\n' "$c"
      return
    fi
  done
  return 1
}

if [[ -z "${KICAD_CLI}" ]]; then
  KICAD_CLI="$(find_kicad_cli || true)"
fi

if [[ -z "${KICAD_CLI}" || ! -x "${KICAD_CLI}" ]]; then
  echo "kicad-cli not found. Install KiCad 8+ (or mount the official DMG) and re-run." >&2
  echo "Projects to check:" >&2
  echo "  ${ROOT}/pcb/current-reference/current-reference.kicad_sch" >&2
  echo "  ${ROOT}/pcb/chaya2mqtt-s2/chaya2mqtt-s2.kicad_sch" >&2
  echo "  ${ROOT}/pcb/chaya2mqtt-s2/chaya2mqtt-s2.kicad_pcb" >&2
  exit 2
fi

CURRENT_REFERENCE_ERC_OUT="${ROOT}/pcb/current-reference/erc"
S2_ERC_OUT="${ROOT}/pcb/chaya2mqtt-s2/production/erc"
S2_DRC_OUT="${ROOT}/pcb/chaya2mqtt-s2/production/drc"
mkdir -p "${CURRENT_REFERENCE_ERC_OUT}" "${S2_ERC_OUT}" "${S2_DRC_OUT}"

echo "Using ${KICAD_CLI}"
"${KICAD_CLI}" version || true

# Text-block reference sheets have no symbols; ERC should be clean (no nets).
"${KICAD_CLI}" sch erc \
  --output "${CURRENT_REFERENCE_ERC_OUT}/current-reference-erc.json" \
  --format json \
  --severity-all \
  "${ROOT}/pcb/current-reference/current-reference.kicad_sch"

"${KICAD_CLI}" sch erc \
  --output "${S2_ERC_OUT}/chaya2mqtt-s2-erc.json" \
  --format json \
  --severity-all \
  "${ROOT}/pcb/chaya2mqtt-s2/chaya2mqtt-s2.kicad_sch"

"${KICAD_CLI}" pcb drc \
  --output "${S2_DRC_OUT}/chaya2mqtt-s2-drc.json" \
  --format json \
  --severity-all \
  "${ROOT}/pcb/chaya2mqtt-s2/chaya2mqtt-s2.kicad_pcb"

echo "ERC report in ${CURRENT_REFERENCE_ERC_OUT}"
echo "S2 ERC report in ${S2_ERC_OUT}"
echo "S2 DRC report in ${S2_DRC_OUT}"
echo "NOTE: EPD_HV VERIFY items in bom.csv remain a manual electrical sign-off gate."
