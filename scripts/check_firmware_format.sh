#!/usr/bin/env bash
# TEST-06: clang-format --dry-run on firmware C/C++ (excludes vendored qrcodegen).
# Required in CI (CI=true). Locally skipped only when clang-format is absent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Prefer versioned clang-format matching CI (LLVM 23). Older majors still accepted locally.
if [[ -z "${CLANG_FORMAT:-}" ]]; then
  for cand in clang-format-23 clang-format-22 clang-format-21 clang-format-18 clang-format; do
    if command -v "$cand" >/dev/null 2>&1; then
      CLANG_FORMAT=$cand
      break
    fi
  done
fi
if [[ -z "${CLANG_FORMAT:-}" ]] || ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
  if [[ "${CI:-}" == "true" || "${CHAYA_REQUIRE_CLANG_FORMAT:-}" == "1" ]]; then
    echo "clang-format is required (install clang-format-23 or set CLANG_FORMAT). CI must not skip TEST-06." >&2
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
