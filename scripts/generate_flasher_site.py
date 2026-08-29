#!/usr/bin/env python3
"""Build a static GitHub Pages tree for the browser web flasher."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FLASHER_DIST = ROOT / "flasher" / "dist"
DEFAULT_OUT = ROOT / "flasher" / "_site"

CALVER_TAG_RE = re.compile(
    r"^v(?P<year>\d{4})\.(?P<month>[1-9]|1[0-2])\.(?P<patch>\d+)(?:-rc\.(?P<rc>[1-9]\d*))?$"
)
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


@dataclass(frozen=True)
class ReleaseRef:
    """Reference a local firmware release and its channel metadata."""

    tag: str
    version: str  # without leading v
    is_prerelease: bool
    factory_bin: Path

    @property
    def channel(self) -> str:
        """Return the release channel name."""
        return "beta" if self.is_prerelease else "stable"

    @property
    def factory_sha256_sidecar(self) -> Path:
        """Sidecar path next to the factory image (firmware.factory.sha256)."""
        return self.factory_bin.with_name("firmware.factory.sha256")


def parse_tag(tag: str) -> tuple[int, int, int, bool, int] | None:
    """Parse a supported CalVer tag into sortable components."""
    m = CALVER_TAG_RE.match(tag)
    if not m:
        return None
    pre = m.group("rc")
    is_pre = pre is not None
    return (
        int(m.group("year")),
        int(m.group("month")),
        int(m.group("patch")),
        is_pre,
        int(pre) if pre is not None else 0,
    )


def tag_sort_key(tag: str) -> tuple[int, int, int, int, int]:
    """Sort CalVer tags newest-first. Stable sorts above beta (-rc.N) of the same base."""
    parsed = parse_tag(tag)
    if parsed is None:
        return (0, 0, 0, 0, 0)
    year, month, patch, is_pre, pre_n = parsed
    # reverse=True: stable (1) sorts above a beta (0) with the same base version.
    not_pre = 0 if is_pre else 1
    return (year, month, patch, not_pre, pre_n if is_pre else 0)


def pick_channels(releases: list[ReleaseRef]) -> dict[str, ReleaseRef]:
    """Pick newest stable and newest beta (by CalVer)."""
    stables = sorted(
        (r for r in releases if not r.is_prerelease),
        key=lambda r: tag_sort_key(r.tag),
        reverse=True,
    )
    betas = sorted(
        (r for r in releases if r.is_prerelease),
        key=lambda r: tag_sort_key(r.tag),
        reverse=True,
    )
    out: dict[str, ReleaseRef] = {}
    if stables:
        out["stable"] = stables[0]
    if betas:
        out["beta"] = betas[0]
    return out


def factory_sha256_hex(factory_bin: Path, sidecar: Path | None = None) -> str:
    """Return the factory image SHA-256 from sidecar (preferred) or by hashing the bin."""
    if sidecar is not None and sidecar.is_file():
        expected = sidecar.read_text(encoding="ascii").strip().split()[0]
        if SHA256_RE.fullmatch(expected) is None:
            message = f"invalid factory SHA-256 sidecar: {sidecar}"
            raise SystemExit(message)
        actual = hashlib.sha256(factory_bin.read_bytes()).hexdigest()
        if actual.lower() != expected.lower():
            message = f"factory SHA-256 mismatch vs sidecar: {factory_bin}"
            raise SystemExit(message)
        return expected.lower()
    return hashlib.sha256(factory_bin.read_bytes()).hexdigest()


def make_manifest(
    name: str,
    version: str,
    factory_filename: str,
    *,
    sha256: str | None = None,
) -> dict[str, Any]:
    """Build an ESP Web Tools manifest for one factory image."""
    part: dict[str, Any] = {"path": factory_filename, "offset": 0}
    if sha256 is not None:
        part["sha256"] = sha256
    return {
        "name": name,
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [part],
            }
        ],
    }


def write_channel_assets(
    out_dir: Path,
    channel: str,
    release: ReleaseRef,
    *,
    project_name: str = "Chaya2MQTT",
) -> dict[str, Any]:
    """Copy a channel's firmware and write its web flasher manifest."""
    channel_dir = out_dir / "firmware" / channel
    channel_dir.mkdir(parents=True, exist_ok=True)
    dest_name = "firmware.factory.bin"
    dest = channel_dir / dest_name
    shutil.copy2(release.factory_bin, dest)

    sha256 = factory_sha256_hex(dest, release.factory_sha256_sidecar)
    sidecar_dest = channel_dir / "firmware.factory.sha256"
    sidecar_dest.write_text(sha256 + "\n", encoding="ascii")

    manifest = make_manifest(project_name, release.version, dest_name, sha256=sha256)
    (channel_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    return {
        "channel": channel,
        "tag": release.tag,
        "version": release.version,
        "manifest": f"firmware/{channel}/manifest.json",
        "factory": f"firmware/{channel}/{dest_name}",
        "sha256": sha256,
    }


def build_site(
    *,
    flasher_dist: Path,
    out_dir: Path,
    releases: list[ReleaseRef],
) -> dict[str, Any]:
    """Build the complete static flasher site and return version metadata."""
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    # Copy the compiled Svelte/Vite app before adding release-specific files.
    for item in flasher_dist.iterdir():
        dest = out_dir / item.name
        if item.is_dir():
            shutil.copytree(item, dest)
        else:
            shutil.copy2(item, dest)

    channels = pick_channels(releases)
    versions: dict[str, Any] = {"channels": {}}
    for channel, release in channels.items():
        versions["channels"][channel] = write_channel_assets(out_dir, channel, release)

    (out_dir / "versions.json").write_text(json.dumps(versions, indent=2) + "\n", encoding="utf-8")
    return versions


def load_releases_from_dir(releases_dir: Path) -> list[ReleaseRef]:
    """Load local firmware releases from a directory.

    Expected layout:
      releases_dir/
        v2026.8.1/firmware.factory.bin
        v2026.8.1/firmware.factory.sha256   (optional; hashed if missing)
        v2026.8.1-rc.1/firmware.factory.bin
    """
    found: list[ReleaseRef] = []
    if not releases_dir.is_dir():
        return found
    for child in sorted(releases_dir.iterdir()):
        if not child.is_dir():
            continue
        tag = child.name
        parsed = parse_tag(tag)
        if parsed is None:
            continue
        factory = child / "firmware.factory.bin"
        if not factory.is_file():
            message = f"missing factory image: {factory}"
            raise SystemExit(message)
        _, _, _, is_pre, _ = parsed
        found.append(
            ReleaseRef(
                tag=tag,
                version=tag.removeprefix("v"),
                is_prerelease=is_pre,
                factory_bin=factory,
            )
        )
    return found


def main(argv: list[str] | None = None) -> int:
    """Build a static flasher site from local release artifacts."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--flasher-dist",
        type=Path,
        default=DEFAULT_FLASHER_DIST,
        help="Compiled Svelte/Vite directory with index.html and assets",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_OUT,
        help="Output site directory",
    )
    parser.add_argument(
        "--releases-dir",
        type=Path,
        required=True,
        help="Directory of tag folders each containing firmware.factory.bin",
    )
    args = parser.parse_args(argv)

    if not args.flasher_dist.is_dir():
        message = (
            f"compiled flasher missing: {args.flasher_dist} — run: cd flasher && npm run build"
        )
        raise SystemExit(message)

    releases = load_releases_from_dir(args.releases_dir)
    if not releases:
        message = f"no CalVer releases found under {args.releases_dir}"
        raise SystemExit(message)

    versions = build_site(
        flasher_dist=args.flasher_dist,
        out_dir=args.out,
        releases=releases,
    )
    print(json.dumps(versions, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
