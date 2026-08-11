#!/usr/bin/env python3
"""
ESP32 core-dump analyzer for chaya2mqtt (PlatformIO).

Reads a raw core-dump image (from the `coredump` partition or a support dump),
locates the matching firmware ELF, and runs xtensa-esp32-elf-gdb for a
backtrace. Does not expose dumps over HTTP — pull the partition locally.

Usage:
    python3 scripts/analyze_coredump.py <coredump.bin> [environment_or_elf]
    python3 scripts/analyze_coredump.py /tmp/coredump.bin
    python3 scripts/analyze_coredump.py /tmp/coredump.bin esp32dev-release
    python3 scripts/analyze_coredump.py /tmp/coredump.bin .pio/build/esp32dev/firmware.elf

How to obtain a dump (esptool):
    esptool.py --chip esp32 --port /dev/ttyUSB0 read_flash 0x3D0000 0x10000 coredump.bin

Partition offsets follow huge_app.csv (coredump @ 0x3D0000, 64 KiB). Verify with:
    pio run -e esp32dev-release -t partitionmap
"""

from __future__ import annotations

import glob
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


DEFAULT_ENV = "esp32dev-release"
ELF_MAGIC = b"\x7fELF"
# ESP-IDF core dump may be wrapped; scan for ELF header.
SCAN_WINDOW = 256 * 1024


def find_build_dir(environment: str) -> Path | None:
    build_dir = Path(".pio/build") / environment
    if build_dir.is_dir():
        return build_dir
    root = Path(".pio/build")
    if root.is_dir():
        envs = sorted(d.name for d in root.iterdir() if d.is_dir())
        print(f"Build directory not found: {build_dir}", file=sys.stderr)
        if envs:
            print(f"Available: {', '.join(envs)}", file=sys.stderr)
    else:
        print("No .pio/build — run: pio run -e esp32dev-release", file=sys.stderr)
    return None


def find_firmware_elf(build_dir: Path) -> Path | None:
    elf = build_dir / "firmware.elf"
    if elf.is_file():
        return elf
    print(f"firmware.elf missing in {build_dir}", file=sys.stderr)
    return None


def find_gdb() -> str | None:
    candidates = [
        "xtensa-esp32-elf-gdb",
        "xtensa-esp-elf-gdb",
        str(Path.home() / ".platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-gdb"),
    ]
    candidates.extend(
        glob.glob(
            str(
                Path.home()
                / ".platformio/packages/toolchain-xtensa-esp*/bin/xtensa-esp*-elf-gdb"
            )
        )
    )
    candidates.extend(
        glob.glob(
            str(
                Path.home()
                / ".espressif/tools/xtensa-esp-elf-gdb/*/xtensa-esp-elf-gdb/bin/xtensa-esp32-elf-gdb"
            )
        )
    )
    for path in candidates:
        if "*" in path:
            continue
        resolved = shutil.which(path) if os.path.sep not in path else path
        if resolved and os.path.isfile(resolved) and os.access(resolved, os.X_OK):
            return resolved
    print("xtensa-esp32-elf-gdb not found (install PlatformIO esp32 toolchain)", file=sys.stderr)
    return None


def extract_elf_coredump(raw: bytes, dest: Path) -> bool:
    if raw.startswith(ELF_MAGIC):
        dest.write_bytes(raw)
        return True
    idx = raw.find(ELF_MAGIC)
    if idx < 0 or idx > SCAN_WINDOW:
        print("No ELF core dump found in image (empty partition or wrong file)", file=sys.stderr)
        return False
    dest.write_bytes(raw[idx:])
    print(f"Extracted ELF core dump at offset {idx}")
    return True


def run_gdb(gdb: str, firmware_elf: Path, core_elf: Path) -> int:
    cmds = [
        "set pagination off",
        "bt full",
        "info registers",
        "quit",
    ]
    cmd_file = core_elf.with_suffix(".gdbcmd")
    cmd_file.write_text("\n".join(cmds) + "\n", encoding="utf-8")
    try:
        proc = subprocess.run(
            [gdb, str(firmware_elf), "-c", str(core_elf), "-x", str(cmd_file), "-batch"],
            check=False,
        )
        return int(proc.returncode)
    finally:
        try:
            cmd_file.unlink()
        except OSError:
            pass


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] in {"-h", "--help"}:
        print(__doc__)
        return 2

    dump_path = Path(argv[1])
    if not dump_path.is_file():
        print(f"File not found: {dump_path}", file=sys.stderr)
        return 1

    env_or_elf = argv[2] if len(argv) > 2 else DEFAULT_ENV
    if Path(env_or_elf).is_file() and env_or_elf.endswith(".elf"):
        firmware_elf = Path(env_or_elf)
    else:
        build_dir = find_build_dir(env_or_elf)
        if build_dir is None:
            return 1
        firmware_elf = find_firmware_elf(build_dir)
        if firmware_elf is None:
            return 1

    gdb = find_gdb()
    if gdb is None:
        return 1

    raw = dump_path.read_bytes()
    if not raw:
        print("Empty dump file", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="chaya-coredump-") as tmp:
        core_elf = Path(tmp) / "core.elf"
        if not extract_elf_coredump(raw, core_elf):
            return 1
        print(f"Firmware ELF: {firmware_elf}")
        print(f"GDB: {gdb}")
        return run_gdb(gdb, firmware_elf, core_elf)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
