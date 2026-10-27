#!/usr/bin/env python3
"""Unit tests for check_firmware_size.py (no PlatformIO required)."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from check_firmware_size import (
    MARKER,
    Sizes,
    fmt_bytes,
    fmt_delta,
    measure,
    parse_elf_size,
    render,
    size_tool_from_packages,
)


class FormatTests(unittest.TestCase):
    """Byte and ELF-size formatting."""

    def test_bytes_and_delta(self) -> None:
        """Format absolute sizes and signed deltas."""
        self.assertEqual(fmt_bytes(100), "100")
        self.assertEqual(fmt_bytes(2048), "2K")
        self.assertEqual(fmt_bytes(None), "—")
        self.assertEqual(fmt_delta(1100, 1000), "+100")
        self.assertEqual(fmt_delta(900, 1000), "-100")
        self.assertEqual(fmt_delta(1000, 1000), "0")
        self.assertEqual(fmt_delta(1000, None), "—")

    def test_elf_size(self) -> None:
        """Parse GNU size -B text/data/bss columns."""
        output = """   text    data     bss     dec     hex filename
 1000     200     300    1500     5dc firmware.elf
"""
        self.assertEqual(parse_elf_size(output), (1200, 500))


class MarkdownTests(unittest.TestCase):
    """PR comment table."""

    def test_table_delta(self) -> None:
        """Show FLASH/RAM growth against the main baseline."""
        current = {
            "esp32s3-release": Sizes(flash=1100, ram=80),
        }
        baseline = {
            "esp32s3-release": Sizes(flash=1000, ram=80),
        }
        text = render(current, baseline, "aaa111", "bbb222")
        self.assertIn(MARKER, text)
        self.assertIn("+100", text)
        self.assertIn("esp32s3-release", text)
        self.assertIn("0", text)


class SizeToolTests(unittest.TestCase):
    """Locate the GNU size binary in PlatformIO package layouts."""

    def test_prefers_pioarduino_toolchain(self) -> None:
        """Use toolchain-xtensa-esp-elf when that is what the build installed."""
        with tempfile.TemporaryDirectory() as tmp:
            packages = Path(tmp)
            tool = packages / "toolchain-xtensa-esp-elf" / "bin" / "xtensa-esp32s3-elf-size"
            tool.parent.mkdir(parents=True)
            tool.write_text("", encoding="utf-8")
            self.assertEqual(size_tool_from_packages(packages), str(tool))

    def test_falls_back_to_classic_s3_toolchain(self) -> None:
        """Accept toolchain-xtensa-esp32s3 when the unified package is absent."""
        with tempfile.TemporaryDirectory() as tmp:
            packages = Path(tmp)
            tool = packages / "toolchain-xtensa-esp32s3" / "bin" / "xtensa-esp32s3-elf-size"
            tool.parent.mkdir(parents=True)
            tool.write_text("", encoding="utf-8")
            self.assertEqual(size_tool_from_packages(packages), str(tool))


class MeasureTests(unittest.TestCase):
    """Read firmware.bin from a fake PlatformIO build tree."""

    def test_reads_firmware_bin(self) -> None:
        """Measure FLASH from firmware.bin when the ELF size tool is absent."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("esp32s3-release",):
                build = root / ".pio" / "build" / name
                build.mkdir(parents=True)
                (build / "firmware.bin").write_bytes(b"\x00" * 50)
            reports = measure(root)
            self.assertEqual(reports["esp32s3-release"].flash, 50)
            self.assertIsNone(reports["esp32s3-release"].ram)


if __name__ == "__main__":
    unittest.main()
