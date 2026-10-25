#!/usr/bin/env bash
# Install clang-format / clang-tidy for CI.
# - clang-format: PyPI wheel pinned to LLVM 23.1.x (matches Homebrew; apt.llvm.org
#   snapshot meta still depends on missing clang-format-23).
# - clang-tidy: apt.llvm.org qualification branch (stable package set).
# Usage: bash scripts/ci_install_clang_tools.sh
set -euo pipefail

LLVM_FORMAT_VERSION="${LLVM_FORMAT_VERSION:-23.1.1}"
LLVM_TIDY_MAJOR="${LLVM_TIDY_MAJOR:-22}"

python3 -m pip install --disable-pip-version-check "clang-format==${LLVM_FORMAT_VERSION}"

. /etc/os-release
codename="${VERSION_CODENAME:-}"
if [[ -z "$codename" ]]; then
  echo "could not detect Ubuntu/Debian codename" >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  SUDO=()
else
  SUDO=(sudo)
fi

wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
  | "${SUDO[@]}" tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null

echo "deb http://apt.llvm.org/${codename}/ llvm-toolchain-${codename}-${LLVM_TIDY_MAJOR} main" \
  | "${SUDO[@]}" tee "/etc/apt/sources.list.d/llvm-${LLVM_TIDY_MAJOR}.list" >/dev/null

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y "clang-tidy-${LLVM_TIDY_MAJOR}"

# Stable name expected by workflows for format (matches local LLVM 23).
format_bin="$(command -v clang-format)"
"${SUDO[@]}" ln -sfn "$format_bin" /usr/local/bin/clang-format-23

clang-format-23 --version
"clang-tidy-${LLVM_TIDY_MAJOR}" --version
