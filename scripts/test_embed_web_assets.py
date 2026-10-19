#!/usr/bin/env python3
"""Smoke tests for SPA blob packing (no ESP32 required)."""

from __future__ import annotations

import gzip
import importlib.util
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Protocol, cast

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "embed_web_assets.py"


class EmbedModule(Protocol):
    """Describe the dynamically loaded asset embedding module."""

    ROOT: Path
    DIST: Path
    OUT_DIR: Path
    MAX_TOTAL_PAYLOAD: int

    def main(self) -> int:
        """Run asset embedding."""
        ...

    def content_type_for(self, path: str) -> str:
        """Return the content type for an asset path."""
        ...

    def cache_class_for(self, path: str) -> str:
        """Return the cache class for an asset path."""
        ...

    def should_gzip(self, path: str) -> bool:
        """Return whether the asset should be gzip-compressed."""
        ...

    def skip_from_blob(self, path: str) -> bool:
        """Return whether the path is omitted from the binary blob."""
        ...


def load_embed() -> EmbedModule:
    """Load the asset embedding script as a typed module."""
    spec = importlib.util.spec_from_file_location("embed_web_assets", SCRIPT)
    if spec is None or spec.loader is None:
        message = f"cannot load module from {SCRIPT}"
        raise ImportError(message)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return cast("EmbedModule", mod)


class EmbedWebAssetsTests(unittest.TestCase):
    """Test deterministic frontend asset embedding."""

    def test_missing_dist_fails(self) -> None:
        """Return an error when the frontend distribution is missing."""
        mod = load_embed()
        with tempfile.TemporaryDirectory() as tmp:
            original = mod.DIST
            mod.DIST = Path(tmp) / "missing"
            mod.OUT_DIR = Path(tmp) / "out"
            try:
                self.assertEqual(mod.main(), 1)
            finally:
                mod.DIST = original

    def test_stale_dist_fails(self) -> None:
        """Reject Vite output older than a frontend source file."""
        mod = load_embed()
        with tempfile.TemporaryDirectory() as tmp:
            frontend = Path(tmp) / "frontend"
            dist = frontend / "dist"
            source_dir = frontend / "src"
            dist.mkdir(parents=True)
            source_dir.mkdir()
            output = dist / "index.html"
            source = source_dir / "main.ts"
            output.write_text("<!doctype html>", encoding="utf-8")
            source.write_text("console.log('new')", encoding="utf-8")
            os.utime(output, ns=(1_000_000_000, 1_000_000_000))
            os.utime(source, ns=(2_000_000_000, 2_000_000_000))

            original_dist = mod.DIST
            original_out = mod.OUT_DIR
            mod.DIST = dist
            mod.OUT_DIR = Path(tmp) / "out"
            try:
                self.assertEqual(mod.main(), 3)
            finally:
                mod.DIST = original_dist
                mod.OUT_DIR = original_out

    def test_deterministic_pack_and_budget(self) -> None:
        """Create deterministic output and enforce the embedded-size budget."""
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
            original_root = mod.ROOT
            original_budget = mod.MAX_TOTAL_PAYLOAD
            mod.ROOT = Path(tmp)
            mod.DIST = dist
            mod.OUT_DIR = out
            try:
                self.assertEqual(mod.main(), 0)
                first_bin = (out / "web_ui.bin").read_bytes()
                first_asm = (out / "web_ui_blob.S").read_text(encoding="utf-8")
                first_manifest = (out / "web_ui_manifest.h").read_text(encoding="utf-8")
                self.assertTrue((out / "web_ui_blob.S").is_file())
                self.assertIn("Blob SHA-256:", first_asm)
                self.assertIn('.incbin "out/web_ui.bin"', first_asm)
                self.assertNotIn(str(Path(tmp)), first_asm)
                # index.html is a C literal only — not packed into the blob.
                self.assertNotIn('{"/index.html"', first_manifest)
                self.assertIn("kWebUiIndexHtml", first_manifest)
                self.assertIn("<!doctype html>", first_manifest)
                self.assertIn("/assets/index-deadbeef.js", first_manifest)
                self.assertIn("SpaCacheClass::Immutable", first_manifest)
                self.assertNotIn(b"<!doctype html>", first_bin)
                js_gz = gzip.compress(b"console.log(1)", compresslevel=9)
                css_gz = gzip.compress(b"body{}", compresslevel=9)
                self.assertIn(js_gz, first_bin)
                self.assertIn(css_gz, first_bin)
                self.assertIn(
                    "gWebUiBlobStart", (out / "web_ui_blob.S").read_text(encoding="utf-8")
                )

                # Re-run must be byte-identical for the blob.
                shutil.rmtree(out)
                self.assertEqual(mod.main(), 0)
                self.assertEqual((out / "web_ui.bin").read_bytes(), first_bin)
                self.assertEqual((out / "web_ui_blob.S").read_text(encoding="utf-8"), first_asm)

                # Changing an asset must change the assembly source so PlatformIO
                # recompiles the .incbin object instead of linking a stale blob.
                (assets / "index-deadbeef.js").write_text("console.log(2)", encoding="utf-8")
                self.assertEqual(mod.main(), 0)
                self.assertNotEqual((out / "web_ui_blob.S").read_text(encoding="utf-8"), first_asm)

                # Budget failure path.
                mod.MAX_TOTAL_PAYLOAD = 1
                self.assertEqual(mod.main(), 2)
            finally:
                mod.ROOT = original_root
                mod.DIST = original_dist
                mod.OUT_DIR = original_out
                mod.MAX_TOTAL_PAYLOAD = original_budget

    def test_content_type_helpers(self) -> None:
        """Map common asset paths to content types and cache classes."""
        mod = load_embed()
        self.assertEqual(mod.content_type_for("/a.html"), "text/html; charset=utf-8")
        self.assertEqual(mod.cache_class_for("/assets/x.js"), "SpaCacheClass::Immutable")
        self.assertEqual(mod.cache_class_for("/index.html"), "SpaCacheClass::NoCache")
        self.assertTrue(mod.should_gzip("/assets/x.js"))
        self.assertTrue(mod.should_gzip("/index.html"))
        self.assertTrue(mod.skip_from_blob("/index.html"))
        self.assertFalse(mod.skip_from_blob("/assets/x.js"))
        raw = b"hello"
        self.assertGreater(len(gzip.compress(raw, compresslevel=9)), 0)


if __name__ == "__main__":
    result = unittest.main(verbosity=2, exit=False).result
    sys.exit(0 if result.wasSuccessful() else 1)
