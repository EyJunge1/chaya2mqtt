#!/usr/bin/env python3
"""Unit tests for the required GxEPD2 source patches."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "patch_gxepd2_busy_wait.py"


def load_patch_module():
    """Load the patch script without a PlatformIO environment."""
    spec = importlib.util.spec_from_file_location("patch_gxepd2_busy_wait", SCRIPT)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load module from {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GxEpdPatchTests(unittest.TestCase):
    """Verify patches are applied, idempotent, and fail closed."""

    def setUp(self) -> None:
        self.mod = load_patch_module()

    def test_busy_patch_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "GxEPD2_EPD.cpp"
            path.write_text(self.mod.BUSY_STOCK, encoding="utf-8")
            self.assertTrue(self.mod.patch_busy(path))
            self.assertIn(self.mod.BUSY_NEW, path.read_text(encoding="utf-8"))
            self.assertFalse(self.mod.patch_busy(path))

    def test_reset_patch_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "panel.cpp"
            path.write_text(self.mod.RESET_STOCK, encoding="utf-8")
            self.assertTrue(self.mod.patch_reset(path))
            self.assertIn(self.mod.RESET_NEW, path.read_text(encoding="utf-8"))
            self.assertFalse(self.mod.patch_reset(path))

    def test_outdated_patch_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "GxEPD2_EPD.cpp"
            path.write_text(f"// {self.mod.BUSY_MARKER}\nold implementation", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                self.mod.patch_busy(path)


if __name__ == "__main__":
    result = unittest.main(verbosity=2, exit=False).result
    sys.exit(0 if result.wasSuccessful() else 1)
