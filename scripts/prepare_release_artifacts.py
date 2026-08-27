#!/usr/bin/env python3
"""Validate and checksum PlatformIO release artifacts for GitHub Releases."""

from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARTITION_TABLE = ROOT / "partitions_chaya_8mb.csv"
# Fixed ESP32-S3 Arduino image locations; the application location comes from the CSV.
FACTORY_FIXED_OFFSETS = (0x0, 0x8000, 0xE000)
# Factory image = bootloader + partitions + boot_app0 + app; keep under flash size.
FACTORY_MAX = 8 * 1024 * 1024


def sha256_hex(path: Path) -> str:
    """Return the SHA-256 checksum of a file as hexadecimal text."""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_partition_number(value: str) -> int:
    """Parse ESP-IDF partition numbers, including hexadecimal and K/M suffixes."""
    raw = value.strip()
    multiplier = 1
    if raw[-1:].lower() == "k":
        multiplier = 1024
        raw = raw[:-1]
    elif raw[-1:].lower() == "m":
        multiplier = 1024 * 1024
        raw = raw[:-1]
    return int(raw, 0) * multiplier


def load_ota_slot(partition_table: Path) -> tuple[int, int]:
    """Return offset and size of the ota_0 application partition."""
    with partition_table.open(newline="", encoding="utf-8") as source:
        for row in csv.reader(source):
            if not row or row[0].lstrip().startswith("#") or len(row) < 5:
                continue
            name, part_type, subtype, offset, size = (field.strip() for field in row[:5])
            if name == "ota_0" and part_type == "app" and subtype == "ota_0":
                return parse_partition_number(offset), parse_partition_number(size)
    message = f"ota_0 app partition missing from {partition_table}"
    raise SystemExit(message)


def validate_factory(path: Path, app_offset: int, firmware: bytes) -> None:
    """Validate factory layout and ensure it embeds the exact OTA application."""
    data = path.read_bytes()
    size = len(data)
    if size == 0:
        message = f"factory image empty: {path}"
        raise SystemExit(message)
    if size > FACTORY_MAX:
        message = f"factory image too large: {size} > {FACTORY_MAX}"
        raise SystemExit(message)
    for offset in (*FACTORY_FIXED_OFFSETS, app_offset):
        if offset >= size:
            message = f"factory image missing data at offset 0x{offset:X}"
            raise SystemExit(message)
        # ESP image magic 0xE9 at bootloader and app; partitions/otadata are not ESP images.
        if offset in (0x0, app_offset) and data[offset] != 0xE9:
            message = (
                f"factory image missing ESP magic 0xE9 at 0x{offset:X} (got 0x{data[offset]:02X})"
            )
            raise SystemExit(message)
    app_end = app_offset + len(firmware)
    if app_end > size or data[app_offset:app_end] != firmware:
        message = "factory image does not contain the exact firmware.bin at the ota_0 offset"
        raise SystemExit(message)


def prepare(build_dir: Path, partition_table: Path = PARTITION_TABLE) -> dict[str, str]:
    """Validate release binaries and write their checksum files."""
    firmware = build_dir / "firmware.bin"
    factory = build_dir / "firmware.factory.bin"
    if not firmware.is_file():
        message = f"missing {firmware}"
        raise SystemExit(message)
    if not factory.is_file():
        message = (
            f"missing {factory} — PlatformIO should emit firmware.factory.bin for esp32s3-release"
        )
        raise SystemExit(message)

    app_offset, app_max = load_ota_slot(partition_table)
    firmware_data = firmware.read_bytes()
    app_size = len(firmware_data)
    if app_size == 0 or firmware_data[0] != 0xE9:
        message = f"firmware.bin is not a non-empty ESP application image: {firmware}"
        raise SystemExit(message)
    if app_size > app_max:
        message = f"firmware.bin too large for OTA slot: {app_size} > {app_max}"
        raise SystemExit(message)

    validate_factory(factory, app_offset, firmware_data)

    sha256 = sha256_hex(firmware)
    factory_sha256 = sha256_hex(factory)

    (build_dir / "firmware.sha256").write_text(sha256 + "\n", encoding="utf-8")
    (build_dir / "firmware.factory.sha256").write_text(factory_sha256 + "\n", encoding="utf-8")

    return {
        "sha256": sha256,
        "factory_sha256": factory_sha256,
        "app_size": str(app_size),
        "factory_size": str(factory.stat().st_size),
    }


def main(argv: list[str] | None = None) -> int:
    """Prepare release artifacts selected by command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path(".pio/build/esp32s3-release"),
        help="PlatformIO build directory",
    )
    parser.add_argument(
        "--github-output",
        type=Path,
        default=None,
        help="Optional path to append key=value lines (GitHub Actions $GITHUB_OUTPUT)",
    )
    parser.add_argument(
        "--partition-table",
        type=Path,
        default=PARTITION_TABLE,
        help="ESP-IDF CSV partition table used for app offset and OTA slot size",
    )
    args = parser.parse_args(argv)

    hashes = prepare(args.build_dir, args.partition_table)
    for key, value in hashes.items():
        print(f"{key}={value}")

    if args.github_output is not None:
        with args.github_output.open("a", encoding="utf-8") as out:
            for key, value in hashes.items():
                out.write(f"{key}={value}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
