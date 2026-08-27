#!/usr/bin/env python3
"""Download Stable + Beta factory images from GitHub Releases for Pages deploy."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from contextlib import contextmanager
from http.client import HTTPResponse, HTTPSConnection
from pathlib import Path
from typing import TYPE_CHECKING
from urllib.parse import urljoin, urlsplit, urlunsplit

if TYPE_CHECKING:
    from collections.abc import Iterator, Mapping

CALVER_TAG_RE = re.compile(r"^v[0-9]{4}\.([1-9]|1[0-2])\.[0-9]+(-rc\.[1-9][0-9]*)?$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
REDIRECT_STATUSES = {301, 302, 303, 307, 308}
FACTORY_MAX_BYTES = 8 * 1024 * 1024
FACTORY_APP_OFFSET = 0x10000
MAX_RELEASE_PAGES = 10


def request_headers(accept: str, token: str | None) -> dict[str, str]:
    """Build common GitHub request headers."""
    headers = {
        "Accept": accept,
        "User-Agent": "chaya2mqtt-flasher-site",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


@contextmanager
def open_https(
    url: str,
    headers: Mapping[str, str],
    timeout: float,
    redirects_left: int = 5,
) -> Iterator[HTTPResponse]:
    """Open an HTTPS URL and follow a bounded number of redirects."""
    parsed = urlsplit(url)
    if parsed.scheme != "https" or parsed.hostname is None:
        message = f"refusing non-HTTPS URL: {url}"
        raise ValueError(message)
    if redirects_left < 0:
        message = f"too many redirects while requesting {url}"
        raise RuntimeError(message)

    target = urlunsplit(("", "", parsed.path or "/", parsed.query, ""))
    connection = HTTPSConnection(parsed.hostname, parsed.port, timeout=timeout)
    connection.request("GET", target, headers=dict(headers))
    response = connection.getresponse()
    try:
        if response.status in REDIRECT_STATUSES:
            location = response.getheader("Location")
            if location is None:
                message = f"redirect without Location header from {url}"
                raise RuntimeError(message)
            redirect_url = urljoin(url, location)
            redirect_headers = dict(headers)
            if urlsplit(redirect_url).hostname != parsed.hostname:
                redirect_headers.pop("Authorization", None)
            with open_https(
                redirect_url,
                redirect_headers,
                timeout,
                redirects_left - 1,
            ) as redirected:
                yield redirected
            return
        if response.status >= 400:
            message = f"HTTP {response.status} while requesting {url}"
            raise RuntimeError(message)
        yield response
    finally:
        response.close()
        connection.close()


def github_api_page(url: str, token: str | None) -> tuple[object, str | None]:
    """Fetch one GitHub API page and return its validated next-page URL."""
    headers = request_headers("application/vnd.github+json", token)
    with open_https(url, headers, timeout=60) as response:
        payload = json.loads(response.read().decode("utf-8"))
        link = response.getheader("Link") or ""
    next_url = None
    for part in link.split(","):
        match = re.fullmatch(r'\s*<([^>]+)>;\s*rel="next"\s*', part)
        if match is None:
            continue
        candidate = match.group(1)
        parsed = urlsplit(candidate)
        if parsed.scheme != "https" or parsed.hostname != "api.github.com":
            message = f"refusing unexpected GitHub pagination URL: {candidate}"
            raise ValueError(message)
        next_url = candidate
        break
    return payload, next_url


def github_api(url: str, token: str | None) -> object:
    """Fetch and decode a single JSON response from the GitHub API."""
    return github_api_page(url, token)[0]


def download(url: str, dest: Path, token: str | None) -> None:
    """Download a release asset to the requested destination."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    headers = request_headers("application/octet-stream", token)
    with open_https(url, headers, timeout=300) as response, dest.open("wb") as out:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)


def validate_factory_download(factory: Path, sidecar: Path) -> None:
    """Verify checksum, size, and ESP image markers before publishing to Pages."""
    expected = sidecar.read_text(encoding="ascii").strip()
    if SHA256_RE.fullmatch(expected) is None:
        message = f"invalid factory SHA-256 sidecar: {sidecar}"
        raise SystemExit(message)
    data = factory.read_bytes()
    if not data or len(data) > FACTORY_MAX_BYTES:
        message = f"invalid factory image size: {len(data)}"
        raise SystemExit(message)
    if data[0] != 0xE9 or len(data) <= FACTORY_APP_OFFSET or data[FACTORY_APP_OFFSET] != 0xE9:
        message = f"factory image lacks ESP image markers: {factory}"
        raise SystemExit(message)
    actual = hashlib.sha256(data).hexdigest()
    if actual.lower() != expected.lower():
        message = f"factory SHA-256 mismatch: {factory}"
        raise SystemExit(message)


def calver_sort_key(tag: str) -> tuple[int, int, int, int]:
    """Return sortable CalVer components for a validated release tag."""
    match = CALVER_TAG_RE.fullmatch(tag)
    if match is None:
        return (0, 0, 0, 0)
    base, _, rc = tag.removeprefix("v").partition("-rc.")
    year, month, patch = (int(part) for part in base.split("."))
    return (year, month, patch, int(rc) if rc else 0)


def pick_latest(releases: list[dict], *, prerelease: bool) -> dict | None:
    """Pick the highest matching CalVer release, independent of GitHub API order."""
    candidates: list[dict] = []
    for rel in releases:
        if rel.get("draft"):
            continue
        tag = rel.get("tag_name") or ""
        if not CALVER_TAG_RE.match(tag):
            continue
        if bool(rel.get("prerelease")) != prerelease:
            continue
        assets = {a.get("name"): a for a in rel.get("assets") or []}
        if "firmware.factory.bin" not in assets or "firmware.factory.sha256" not in assets:
            continue
        candidates.append(rel)
    return max(candidates, key=lambda rel: calver_sort_key(str(rel["tag_name"])), default=None)


def write_release(rel: dict, out_root: Path, token: str | None) -> Path:
    """Download a release's factory image and return its output directory."""
    tag = rel["tag_name"]
    assets = {a["name"]: a for a in rel["assets"]}
    factory = assets["firmware.factory.bin"]
    factory_sha256 = assets["firmware.factory.sha256"]
    dest_dir = out_root / tag
    dest = dest_dir / "firmware.factory.bin"
    sidecar_dest = dest_dir / "firmware.factory.sha256"
    # Prefer the API asset URL; it supports private-repository downloads with GITHUB_TOKEN.
    url = factory.get("url") or factory.get("browser_download_url")
    sidecar_url = factory_sha256.get("url") or factory_sha256.get("browser_download_url")
    if not url or not sidecar_url:
        message = f"missing factory download URL in {tag}"
        raise SystemExit(message)
    download(url, dest, token)
    download(sidecar_url, sidecar_dest, token)
    validate_factory_download(dest, sidecar_dest)
    return dest_dir


def main(argv: list[str] | None = None) -> int:
    """Download the latest stable and prerelease factory images."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        default=os.environ.get("GITHUB_REPOSITORY", "EyJunge1/chaya2mqtt"),
        help="owner/name",
    )
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="Directory that will contain vTAG/firmware.factory.bin folders",
    )
    parser.add_argument(
        "--token",
        default=os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN"),
        help="GitHub token (needed for private repos / higher rate limits)",
    )
    args = parser.parse_args(argv)

    api: str | None = f"https://api.github.com/repos/{args.repo}/releases?per_page=100"
    releases: list[dict] = []
    try:
        for _ in range(MAX_RELEASE_PAGES):
            if api is None:
                break
            page, api = github_api_page(api, args.token)
            if not isinstance(page, list):
                message = "unexpected releases API payload"
                raise SystemExit(message)
            releases.extend(item for item in page if isinstance(item, dict))
    except (OSError, RuntimeError, ValueError) as exc:
        message = f"GitHub API error: {exc}"
        raise SystemExit(message) from exc

    if api is not None:
        print(
            f"warning: release pagination stopped after {MAX_RELEASE_PAGES} pages",
            file=sys.stderr,
        )

    args.out.mkdir(parents=True, exist_ok=True)
    selected: list[dict] = []
    stable = pick_latest(releases, prerelease=False)
    beta = pick_latest(releases, prerelease=True)
    if stable:
        selected.append(stable)
    if beta:
        selected.append(beta)

    if not selected:
        message = "no releases with factory image and checksum found — publish a release first"
        raise SystemExit(message)

    for rel in selected:
        path = write_release(rel, args.out, args.token)
        print(f"downloaded {rel['tag_name']} -> {path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
