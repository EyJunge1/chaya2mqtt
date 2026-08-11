#!/usr/bin/env python3
"""Smoke tests for SPA blob packing (no ESP32 required)."""

from __future__ import annotations

import gzip
import importlib.util
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "embed_web_assets.py"


def load_embed():
    spec = importlib.util.spec_from_file_location("embed_web_assets", SCRIPT)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class EmbedWebAssetsTests(unittest.TestCase):
    def test_missing_dist_fails(self) -> None:
        mod = load_embed()
        with tempfile.TemporaryDirectory() as tmp:
            original = mod.DIST
            mod.DIST = Path(tmp) / "missing"
            mod.OUT_DIR = Path(tmp) / "out"
            try:
                self.assertEqual(mod.main(), 1)
            finally:
                mod.DIST = original

    def test_deterministic_pack_and_budget(self) -> None:
        mod = load_embed()
        with tempfile.TemporaryDirectory() as tmp:
            dist = Path(tmp) / "dist"
            out = Path(tmp) / "out"
            dist.mkdir()
            (dist / "index.html").write_text("<!doctype html><title>t</title>", encoding="utf-8")
            assets = dist / "assets"
            assets.mkdir()
            (assets / "index-deadbeef.js").write_text("console.log(1)", encoding="utf-8")
            (assets / "index-deadbeef.css").write_text("body{}", encoding="utf-8")

            original_dist = mod.DIST
            original_out = mod.OUT_DIR
            original_budget = mod.MAX_TOTAL_GZ
            mod.DIST = dist
            mod.OUT_DIR = out
            try:
                self.assertEqual(mod.main(), 0)
                first_bin = (out / "web_ui.bin").read_bytes()
                first_manifest = (out / "web_ui_manifest.h").read_text(encoding="utf-8")
                self.assertTrue((out / "web_ui_blob.S").is_file())
                self.assertIn("/index.html", first_manifest)
                self.assertIn("/assets/index-deadbeef.js", first_manifest)
                self.assertIn("SpaCacheClass::Immutable", first_manifest)
                self.assertIn("gWebUiBlobStart", (out / "web_ui_blob.S").read_text(encoding="utf-8"))

                # Re-run must be byte-identical for the blob.
                shutil.rmtree(out)
                self.assertEqual(mod.main(), 0)
                self.assertEqual((out / "web_ui.bin").read_bytes(), first_bin)

                # Budget failure path.
                mod.MAX_TOTAL_GZ = 1
                self.assertEqual(mod.main(), 2)
            finally:
                mod.DIST = original_dist
                mod.OUT_DIR = original_out
                mod.MAX_TOTAL_GZ = original_budget

    def test_content_type_helpers(self) -> None:
        mod = load_embed()
        self.assertEqual(mod.content_type_for("/a.html"), "text/html; charset=utf-8")
        self.assertEqual(mod.cache_class_for("/assets/x.js"), "SpaCacheClass::Immutable")
        self.assertEqual(mod.cache_class_for("/index.html"), "SpaCacheClass::NoCache")
        raw = b"hello"
        self.assertGreater(len(gzip.compress(raw, compresslevel=9)), 0)


if __name__ == "__main__":
    result = unittest.main(verbosity=2, exit=False).result
    sys.exit(0 if result.wasSuccessful() else 1)
