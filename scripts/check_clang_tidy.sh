#!/usr/bin/env bash
# TEST-05: clang-tidy on host-pure headers (no Arduino/FreeRTOS/ESP).
# Required in CI (CI=true). Locally skipped only when clang-tidy is absent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -z "${CLANG_TIDY:-}" ]]; then
  for cand in clang-tidy-23 clang-tidy-22 clang-tidy-21 clang-tidy-18 clang-tidy; do
    if command -v "$cand" >/dev/null 2>&1; then
      CLANG_TIDY=$cand
      break
    fi
  done
fi
if [[ -z "${CLANG_TIDY:-}" ]] || ! command -v "$CLANG_TIDY" >/dev/null 2>&1; then
  if [[ "${CI:-}" == "true" || "${CHAYA_REQUIRE_CLANG_TIDY:-}" == "1" ]]; then
    echo "clang-tidy is required (install clang-tidy-23 or set CLANG_TIDY). CI must not skip TEST-05." >&2
    exit 1
  fi
  echo "clang-tidy not installed — skipping host-pure header lint (TEST-05)"
  exit 0
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

# Extra host-pure headers not named *_pure.h (see docs/TESTING.md).
required_extra=(
  src/mqtt/backoff.h
  src/mqtt/counter_payload.h
  src/mqtt/mqtt_config.h
  src/mqtt/mqtt_publish_ack.h
  src/mqtt/pairing.h
  src/ota/ota_health.h
  src/ota/ota_url_allow.h
  src/ota/version_cmp.h
  src/util/net_validate.h
  src/util/time_helpers.h
  src/web/host_validate.h
  src/web/spa_asset_lookup.h
  src/wifi/wlan_pack.h
  src/wifi/wlan_recovery.h
  src/wifi/wlan_soft_reconnect.h
)

cxx_incs=(-std=c++23 -I"$ROOT/src" -I"$ROOT")
arduinojson_inc="$ROOT/.pio/libdeps/native/ArduinoJson/src"
if [[ -d "$arduinojson_inc" ]]; then
  cxx_incs+=(-I"$arduinojson_inc")
fi

header_compiles() {
  local h="$1"
  local stub="$tmpdir/probe_$(basename "$h").cpp"
  cat >"$stub" <<EOF
#include "$ROOT/$h"
auto main() -> int { return 0; }
EOF
  c++ -fsyntax-only "${cxx_incs[@]}" "$stub" >/dev/null 2>&1
}

declare -a headers=()
seen_file="$tmpdir/seen"
: >"$seen_file"

add_header() {
  local h="$1"
  [[ -n "$h" ]] || return 0
  [[ -f "$h" ]] || return 0
  case "$h" in
    src/display/qr/*|src/web/assets/*) return 0 ;;
  esac
  if grep -Fxq "$h" "$seen_file"; then
    return 0
  fi
  printf '%s\n' "$h" >>"$seen_file"
  headers+=("$h")
}

while IFS= read -r h; do
  add_header "$h"
done < <(find src -name '*_pure.h' | LC_ALL=C sort)

for h in "${required_extra[@]}"; do
  add_header "$h"
done

# Optional: other headers that compile as host C++ (after pio test, ArduinoJson may be present).
while IFS= read -r h; do
  [[ -n "$h" ]] || continue
  if grep -Fxq "$h" "$seen_file"; then
    continue
  fi
  if header_compiles "$h"; then
    add_header "$h"
  fi
done < <(find src -name '*.h' ! -path 'src/display/qr/*' | LC_ALL=C sort)

found=0
fails=0
for h in "${headers[@]}"; do
  found=$((found + 1))
  stub="$tmpdir/$(basename "$h").cpp"
  cat >"$stub" <<EOF
#include "$ROOT/$h"
auto main() -> int { return 0; }
EOF
  if ! "$CLANG_TIDY" "$stub" --config-file="$ROOT/.clang-tidy" -- \
      "${cxx_incs[@]}" >"$tmpdir/out" 2>"$tmpdir/err"; then
    echo "clang-tidy failed for $h:"
    cat "$tmpdir/out" "$tmpdir/err" || true
    fails=$((fails + 1))
  else
    echo "ok $h"
  fi
done

if [[ "$found" -eq 0 ]]; then
  echo "no host-pure headers found"
  exit 1
fi
if [[ "$fails" -ne 0 ]]; then
  echo "$fails host-pure header(s) failed clang-tidy"
  exit 1
fi
echo "clang-tidy host-pure headers ($CLANG_TIDY): $found ok"
