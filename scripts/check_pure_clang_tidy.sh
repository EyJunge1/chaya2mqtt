#!/usr/bin/env bash
# TEST-05: clang-tidy on header-only pure helpers (when clang-tidy is available).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy not installed — skipping pure-header lint (TEST-05)"
  exit 0
fi

mapfile -t headers < <(find src -name '*_pure.h' | sort)
if [[ ${#headers[@]} -eq 0 ]]; then
  echo "no *_pure.h files found"
  exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

# Compile each pure header in isolation (C++17, host).
fails=0
for h in "${headers[@]}"; do
  stub="$tmpdir/$(basename "$h").cpp"
  cat >"$stub" <<EOF
#include "$ROOT/$h"
int main() { return 0; }
EOF
  if ! clang-tidy "$stub" -- -std=c++17 -I"$ROOT/src" -I"$ROOT" >/dev/null 2>"$tmpdir/err"; then
    echo "clang-tidy failed for $h:"
    cat "$tmpdir/err" || true
    fails=$((fails + 1))
  else
    echo "ok $h"
  fi
done

if [[ "$fails" -ne 0 ]]; then
  echo "$fails pure header(s) failed clang-tidy"
  exit 1
fi
echo "clang-tidy pure headers: ${#headers[@]} ok"
