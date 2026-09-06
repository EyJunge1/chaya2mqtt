#!/usr/bin/env bash
# Apply .clang-format to firmware C/C++ (excludes vendored qrcodegen).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is required (install it or set PATH). Homebrew: llvm on PATH." >&2
  exit 1
fi

files=()
while IFS= read -r f; do
  [[ -n "$f" ]] || continue
  files+=("$f")
done < <(find src sim test \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) ! -path 'src/display/qr/qrcodegen.c' ! -path 'src/display/qr/qrcodegen.h' | LC_ALL=C sort)

if [[ "${#files[@]}" -eq 0 ]]; then
  echo "no C/C++ files to format"
  exit 1
fi

clang-format -i "${files[@]}"
echo "clang-format: ${#files[@]} files"
