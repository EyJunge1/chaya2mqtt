#!/usr/bin/env python3
"""Unit tests for release artifact prep and flasher site generation."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_module(name: str, relative: str):
    """Load a repository script under a stable module name."""
    path = ROOT / relative
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        message = f"cannot load module from {path}"
        raise ImportError(message)
    mod = importlib.util.module_from_spec(spec)
    # Required so @dataclass works under Python 3.14 when loading via importlib.
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


class PrepareReleaseArtifactsTests(unittest.TestCase):
    """Test release artifact validation and checksum generation."""

    def setUp(self) -> None:
        """Load the release artifact module for each test."""
        self.mod = load_module("prepare_release_artifacts", "scripts/prepare_release_artifacts.py")

    def _write_factory(
        self,
        path: Path,
        firmware: bytes,
        size: int = 0x11000,
        app_offset: int = 0x10000,
    ) -> None:
        data = bytearray(max(size, app_offset + len(firmware)))
        data[0x0] = 0xE9
        data[app_offset : app_offset + len(firmware)] = firmware
        path.write_bytes(bytes(data))

    def test_prepare_writes_checksums(self) -> None:
        """Write all expected checksum files for valid firmware images."""
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            firmware = b"\xE9app-image-bytes"
            (build / "firmware.bin").write_bytes(firmware)
            self._write_factory(build / "firmware.factory.bin", firmware)
            hashes = self.mod.prepare(build)
            self.assertEqual(len(hashes["sha256"]), 64)
            self.assertEqual(len(hashes["factory_sha256"]), 64)
            self.assertEqual(
                (build / "firmware.sha256").read_text(encoding="utf-8").strip(), hashes["sha256"]
            )
            self.assertEqual(
                (build / "firmware.factory.sha256").read_text(encoding="utf-8").strip(),
                hashes["factory_sha256"],
            )

    def test_rejects_missing_magic(self) -> None:
        """Reject a factory image without ESP image markers."""
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            (build / "firmware.bin").write_bytes(b"\xE9app")
            (build / "firmware.factory.bin").write_bytes(b"\x00" * 0x11000)
            with self.assertRaises(SystemExit):
                self.mod.prepare(build)

    def test_rejects_oversized_ota_app(self) -> None:
        """Reject firmware that exceeds the configured OTA slot."""
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            _, ota_size = self.mod.load_ota_slot(self.mod.PARTITION_TABLE)
            firmware = b"\xE9" + b"x" * ota_size
            (build / "firmware.bin").write_bytes(firmware)
            self._write_factory(build / "firmware.factory.bin", b"\xE9")
            with self.assertRaises(SystemExit):
                self.mod.prepare(build)

    def test_uses_partition_table_for_app_layout(self) -> None:
        """Derive factory app offset and maximum OTA size from the CSV."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build = root / "build"
            build.mkdir()
            table = root / "partitions.csv"
            table.write_text(
                "# Name, Type, SubType, Offset, Size\n"
                "ota_0, app, ota_0, 0x20000, 4K\n",
                encoding="utf-8",
            )
            firmware = b"\xE9app"
            (build / "firmware.bin").write_bytes(firmware)
            self._write_factory(
                build / "firmware.factory.bin",
                firmware,
                size=0x21000,
                app_offset=0x20000,
            )

            hashes = self.mod.prepare(build, table)

            self.assertEqual(hashes["app_size"], str(len(firmware)))

    def test_rejects_factory_with_different_application(self) -> None:
        """Reject a factory image built from a different firmware.bin."""
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            (build / "firmware.bin").write_bytes(b"\xE9expected")
            self._write_factory(build / "firmware.factory.bin", b"\xE9different")
            with self.assertRaises(SystemExit):
                self.mod.prepare(build)


class GenerateFlasherSiteTests(unittest.TestCase):
    """Test static web flasher site generation."""

    def setUp(self) -> None:
        """Load the flasher site generator for each test."""
        self.mod = load_module("generate_flasher_site", "scripts/generate_flasher_site.py")

    def test_tag_sort_stable_above_beta_same_base(self) -> None:
        """Sort stable releases above prereleases with the same base version."""
        tags = ["v2026.8.1-rc.2", "v2026.8.1", "v2026.8.1-rc.1", "v2026.7.9"]
        ordered = sorted(tags, key=self.mod.tag_sort_key, reverse=True)
        self.assertEqual(ordered[0], "v2026.8.1")
        self.assertEqual(ordered[1], "v2026.8.1-rc.2")
        self.assertEqual(ordered[-1], "v2026.7.9")

    def test_pick_channels(self) -> None:
        """Select the newest release independently for each channel."""
        release_ref = self.mod.ReleaseRef
        releases = [
            release_ref("v2026.8.1-rc.1", "2026.8.1-rc.1", True, Path("a")),
            release_ref("v2026.8.0", "2026.8.0", False, Path("b")),
            release_ref("v2026.8.1-rc.2", "2026.8.1-rc.2", True, Path("c")),
            release_ref("v2026.7.1", "2026.7.1", False, Path("d")),
        ]
        channels = self.mod.pick_channels(releases)
        self.assertEqual(channels["stable"].tag, "v2026.8.0")
        self.assertEqual(channels["beta"].tag, "v2026.8.1-rc.2")

    def test_manifest_offsets(self) -> None:
        """Generate an ESP Web Tools manifest with the expected app offset."""
        manifest = self.mod.make_manifest("Chaya2MQTT", "2026.8.1", "firmware.factory.bin")
        self.assertEqual(manifest["builds"][0]["chipFamily"], "ESP32-S3")
        self.assertEqual(manifest["builds"][0]["parts"][0]["offset"], 0)
        self.assertTrue(manifest["new_install_prompt_erase"])
        self.assertEqual(manifest["new_install_improv_wait_time"], 0)

    def test_build_site_copies_channels(self) -> None:
        """Copy stable and beta firmware into the generated site."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            flasher_dist = root / "dist"
            flasher_dist.mkdir()
            (flasher_dist / "index.html").write_text(
                "<!doctype html><title>f</title>", encoding="utf-8"
            )
            releases_dir = root / "releases"
            for tag in ("v2026.8.1", "v2026.8.2-rc.1"):
                d = releases_dir / tag
                d.mkdir(parents=True)
                (d / "firmware.factory.bin").write_bytes(b"factory-" + tag.encode())

            out = root / "_site"
            releases = self.mod.load_releases_from_dir(releases_dir)
            versions = self.mod.build_site(
                flasher_dist=flasher_dist,
                out_dir=out,
                releases=releases,
            )

            self.assertIn("stable", versions["channels"])
            self.assertIn("beta", versions["channels"])
            self.assertTrue((out / "index.html").is_file())
            self.assertTrue((out / "versions.json").is_file())
            self.assertTrue((out / "firmware" / "stable" / "manifest.json").is_file())
            self.assertTrue((out / "firmware" / "beta" / "firmware.factory.bin").is_file())

            stable_manifest = json.loads(
                (out / "firmware" / "stable" / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(stable_manifest["version"], "2026.8.1")
            self.assertEqual(
                stable_manifest["builds"][0]["parts"][0]["path"], "firmware.factory.bin"
            )


class FetchFlasherReleasesTests(unittest.TestCase):
    """Test GitHub release selection for the Pages flasher."""

    def setUp(self) -> None:
        """Load the release fetcher for each test."""
        self.mod = load_module("fetch_flasher_releases", "scripts/fetch_flasher_releases.py")

    def test_pick_latest_uses_calver_not_api_order(self) -> None:
        """Do not serve an older release merely because GitHub returns it first."""
        assets = [
            {"name": "firmware.factory.bin"},
            {"name": "firmware.factory.sha256"},
        ]
        releases = [
            {
                "tag_name": "v2026.7.9",
                "draft": False,
                "prerelease": False,
                "assets": assets,
            },
            {
                "tag_name": "v2026.8.1",
                "draft": False,
                "prerelease": False,
                "assets": assets,
            },
        ]
        selected = self.mod.pick_latest(releases, prerelease=False)
        self.assertIsNotNone(selected)
        self.assertEqual(selected["tag_name"], "v2026.8.1")

    def test_main_follows_release_pagination(self) -> None:
        """Select releases across all API pages, not only the first response."""
        assets = [
            {"name": "firmware.factory.bin"},
            {"name": "firmware.factory.sha256"},
        ]
        pages = iter(
            [
                (
                    [
                        {
                            "tag_name": "v2026.7.1",
                            "draft": False,
                            "prerelease": False,
                            "assets": assets,
                        }
                    ],
                    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases?page=2",
                ),
                (
                    [
                        {
                            "tag_name": "v2026.8.1",
                            "draft": False,
                            "prerelease": False,
                            "assets": assets,
                        }
                    ],
                    None,
                ),
            ]
        )
        written: list[str] = []
        original_api = self.mod.github_api_page
        original_write = self.mod.write_release
        self.mod.github_api_page = lambda _url, _token: next(pages)
        self.mod.write_release = (
            lambda rel, out, _token: written.append(rel["tag_name"]) or out / rel["tag_name"]
        )
        try:
            with tempfile.TemporaryDirectory() as tmp:
                self.assertEqual(self.mod.main(["--out", tmp]), 0)
        finally:
            self.mod.github_api_page = original_api
            self.mod.write_release = original_write
        self.assertEqual(written, ["v2026.8.1"])

    def test_validate_factory_download_checks_hash_and_markers(self) -> None:
        """Accept only a checksummed merged image with bootloader and app markers."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            factory = root / "firmware.factory.bin"
            sidecar = root / "firmware.factory.sha256"
            data = bytearray(0x10001)
            data[0] = 0xE9
            data[0x10000] = 0xE9
            factory.write_bytes(data)
            sidecar.write_text(hashlib.sha256(data).hexdigest() + "\n", encoding="ascii")
            self.mod.validate_factory_download(factory, sidecar)

            sidecar.write_text("0" * 64 + "\n", encoding="ascii")
            with self.assertRaises(SystemExit):
                self.mod.validate_factory_download(factory, sidecar)


if __name__ == "__main__":
    unittest.main()
