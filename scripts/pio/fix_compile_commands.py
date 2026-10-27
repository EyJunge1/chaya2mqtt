#!/usr/bin/env python3
"""Normalize compile_commands.json for clangd (absolute paths + -Isrc)."""

from __future__ import annotations

import json
import os
import sys


def main() -> int:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
    path = os.path.join(root, "compile_commands.json")
    if not os.path.isfile(path):
        return 0

    with open(path, encoding="utf-8") as f:
        db = json.load(f)

    src_inc = os.path.join(root, "src")
    epd_inc = os.path.join(root, "src", "epd")
    changed = False

    for entry in db:
        directory = entry.get("directory") or root
        file_path = entry.get("file") or ""
        if file_path and not os.path.isabs(file_path):
            entry["file"] = os.path.normpath(os.path.join(directory, file_path))
            changed = True

        cmd = entry.get("command")
        if not cmd:
            continue
        extras = []
        if f"-I{src_inc}" not in cmd:
            extras.append(f"-I{src_inc}")
        if f"-I{epd_inc}" not in cmd:
            extras.append(f"-I{epd_inc}")
        if extras:
            parts = cmd.split(" ", 1)
            entry["command"] = parts[0] + " " + " ".join(extras) + (" " + parts[1] if len(parts) > 1 else "")
            changed = True

    if changed:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(db, f)
        print(f"fix_compile_commands: updated {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
