#!/usr/bin/env python3
"""Unit tests for check_firmware_size.py."""

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
    size_tool_from_espressif,
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
            "chaya-fw": Sizes(flash=1100, ram=80),
        }
        baseline = {
            "chaya-fw": Sizes(flash=1000, ram=80),
        }
        text = render(current, baseline, "aaa111", "bbb222")
        self.assertIn(MARKER, text)
        self.assertIn("+100", text)
        self.assertIn("chaya-fw", text)
        self.assertIn("0", text)


class SizeToolTests(unittest.TestCase):
    """Locate the GNU size binary in the espup/espressif tools layout."""

    def test_finds_espressif_toolchain(self) -> None:
        """Resolve xtensa-esp-elf-size under ~/.espressif/tools-style trees."""
        with tempfile.TemporaryDirectory() as tmp:
            tools = Path(tmp)
            tool = (
                tools
                / "xtensa-esp-elf"
                / "14.2.0_20241119"
                / "xtensa-esp-elf"
                / "bin"
                / "xtensa-esp-elf-size"
            )
            tool.parent.mkdir(parents=True)
            tool.write_text("", encoding="utf-8")
            self.assertEqual(size_tool_from_espressif(tools), str(tool))

    def test_prefers_newest_version(self) -> None:
        """When multiple versions exist, pick the last sorted path."""
        with tempfile.TemporaryDirectory() as tmp:
            tools = Path(tmp)
            older = (
                tools
                / "xtensa-esp-elf"
                / "13.2.0_20230928"
                / "xtensa-esp-elf"
                / "bin"
                / "xtensa-esp-elf-size"
            )
            newer = (
                tools
                / "xtensa-esp-elf"
                / "14.2.0_20241119"
                / "xtensa-esp-elf"
                / "bin"
                / "xtensa-esp-elf-size"
            )
            for tool in (older, newer):
                tool.parent.mkdir(parents=True)
                tool.write_text("", encoding="utf-8")
            self.assertEqual(size_tool_from_espressif(tools), str(newer))

    def test_missing_tools_dir(self) -> None:
        """Return None when the tools root does not exist."""
        self.assertIsNone(size_tool_from_espressif(Path("/nonexistent/espressif/tools")))


class MeasureTests(unittest.TestCase):
    """Read firmware.bin from the Rust release output tree."""

    def test_reads_firmware_bin(self) -> None:
        """Measure FLASH from firmware.bin when the ELF size tool is absent."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build = root / "out" / "rust-release"
            build.mkdir(parents=True)
            (build / "firmware.bin").write_bytes(b"\x00" * 50)
            reports = measure(root)
            self.assertEqual(reports["chaya-fw"].flash, 50)
            self.assertIsNone(reports["chaya-fw"].ram)


if __name__ == "__main__":
    unittest.main()
