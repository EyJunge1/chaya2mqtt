#!/usr/bin/env bash
# Install versioned clang-format / clang-tidy for CI (matches Homebrew LLVM major).
# Usage: sudo bash scripts/ci_install_clang_tools.sh
set -euo pipefail

LLVM_MAJOR="${LLVM_MAJOR:-23}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "run as root: sudo bash $0" >&2
  exit 1
fi

. /etc/os-release
codename="${VERSION_CODENAME:-}"
if [[ -z "$codename" ]]; then
  echo "could not detect Ubuntu/Debian codename" >&2
  exit 1
fi

wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
  | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null

# Development branch packages are named clang-format-23 / clang-tidy-23 under "main".
# Stable/qualification branches use llvm-toolchain-${codename}-${LLVM_MAJOR}.
if [[ "$LLVM_MAJOR" == "23" ]]; then
  repo="llvm-toolchain-${codename}"
else
  repo="llvm-toolchain-${codename}-${LLVM_MAJOR}"
fi

echo "deb http://apt.llvm.org/${codename}/ ${repo} main" \
  >"/etc/apt/sources.list.d/llvm-${LLVM_MAJOR}.list"

apt-get update
apt-get install -y "clang-format-${LLVM_MAJOR}" "clang-tidy-${LLVM_MAJOR}"

"clang-format-${LLVM_MAJOR}" --version
"clang-tidy-${LLVM_MAJOR}" --version
