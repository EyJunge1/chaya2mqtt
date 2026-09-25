#!/usr/bin/env bash
# Install versioned clang-format / clang-tidy for CI (matches Homebrew LLVM 23.1.x).
# Uses the official GitHub release tarball; apt.llvm.org often lacks clang-*-23 packages.
# Usage: sudo bash scripts/ci_install_clang_tools.sh
set -euo pipefail

LLVM_VERSION="${LLVM_VERSION:-23.1.2}"
LLVM_MAJOR="${LLVM_VERSION%%.*}"
PREFIX="${LLVM_PREFIX:-/opt/llvm-${LLVM_MAJOR}}"
ASSET="LLVM-${LLVM_VERSION}-Linux-X64.tar.xz"
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${ASSET}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "run as root: sudo bash $0" >&2
  exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

echo "Downloading ${URL}"
wget -qO "${tmpdir}/${ASSET}" "$URL"

mkdir -p "${PREFIX}/bin"
tar -xJf "${tmpdir}/${ASSET}" -C "$tmpdir"
src_bin="${tmpdir}/LLVM-${LLVM_VERSION}-Linux-X64/bin"
install -m 0755 "${src_bin}/clang-format" "${PREFIX}/bin/clang-format"
install -m 0755 "${src_bin}/clang-tidy" "${PREFIX}/bin/clang-tidy"

ln -sfn "${PREFIX}/bin/clang-format" "/usr/local/bin/clang-format-${LLVM_MAJOR}"
ln -sfn "${PREFIX}/bin/clang-tidy" "/usr/local/bin/clang-tidy-${LLVM_MAJOR}"
# Unversioned names help scripts that look for plain clang-format / clang-tidy.
ln -sfn "${PREFIX}/bin/clang-format" /usr/local/bin/clang-format
ln -sfn "${PREFIX}/bin/clang-tidy" /usr/local/bin/clang-tidy

"clang-format-${LLVM_MAJOR}" --version
"clang-tidy-${LLVM_MAJOR}" --version
