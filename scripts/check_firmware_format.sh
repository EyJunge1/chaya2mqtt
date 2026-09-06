#!/usr/bin/env bash
# TEST-06: clang-format --dry-run on firmware C/C++ (excludes vendored qrcodegen).
# Required in CI (CI=true). Locally skipped only when clang-format is absent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Prefer clang-format-18 (Ubuntu CI). LLVM 19+ wraps some signatures differently.
if [[ -z "${CLANG_FORMAT:-}" ]]; then
  if command -v clang-format-18 >/dev/null 2>&1; then
    CLANG_FORMAT=clang-format-18
  elif command -v clang-format >/dev/null 2>&1; then
    CLANG_FORMAT=clang-format
  fi
fi
if [[ -z "${CLANG_FORMAT:-}" ]] || ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
  if [[ "${CI:-}" == "true" || "${CHAYA_REQUIRE_CLANG_FORMAT:-}" == "1" ]]; then
    echo "clang-format is required (install clang-format-18 or set CLANG_FORMAT). CI must not skip TEST-06." >&2
    exit 1
  fi
  echo "clang-format not installed — skipping firmware format check (TEST-06)"
  exit 0
fi

files=()
while IFS= read -r f; do
  [[ -n "$f" ]] || continue
  files+=("$f")
done < <(find src sim test \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) ! -path 'src/display/qr/qrcodegen.c' ! -path 'src/display/qr/qrcodegen.h' | LC_ALL=C sort)

if [[ "${#files[@]}" -eq 0 ]]; then
  echo "no C/C++ files to format-check"
  exit 1
fi

"$CLANG_FORMAT" --dry-run -Werror "${files[@]}"
echo "clang-format dry-run ($CLANG_FORMAT): ${#files[@]} ok"
