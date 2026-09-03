#!/usr/bin/env bash
# TEST-05: clang-tidy on header-only pure helpers.
# Required in CI (CI=true). Locally skipped only when clang-tidy is absent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v clang-tidy >/dev/null 2>&1; then
  if [[ "${CI:-}" == "true" || "${CHAYA_REQUIRE_CLANG_TIDY:-}" == "1" ]]; then
    echo "clang-tidy is required (install it or set PATH). CI must not skip TEST-05." >&2
    exit 1
  fi
  echo "clang-tidy not installed — skipping pure-header lint (TEST-05)"
  exit 0
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

found=0
fails=0
# Process substitution: bash 3.2 (macOS) has no mapfile.
while IFS= read -r h; do
  [[ -n "$h" ]] || continue
  found=$((found + 1))
  stub="$tmpdir/$(basename "$h").cpp"
  cat >"$stub" <<EOF
#include "$ROOT/$h"
int main() { return 0; }
EOF
  # LLVM 21+: no default checks. Same set as classic clang-tidy / Ubuntu packages.
  if ! clang-tidy "$stub" \
      -checks='clang-diagnostic-*,clang-analyzer-*' \
      -warnings-as-errors='clang-diagnostic-*,clang-analyzer-*' \
      -- -std=c++17 -I"$ROOT/src" -I"$ROOT" >/dev/null 2>"$tmpdir/err"; then
    echo "clang-tidy failed for $h:"
    cat "$tmpdir/err" || true
    fails=$((fails + 1))
  else
    echo "ok $h"
  fi
done < <(find src -name '*_pure.h' | LC_ALL=C sort)

if [[ "$found" -eq 0 ]]; then
  echo "no *_pure.h files found"
  exit 1
fi
if [[ "$fails" -ne 0 ]]; then
  echo "$fails pure header(s) failed clang-tidy"
  exit 1
fi
echo "clang-tidy pure headers: $found ok"
