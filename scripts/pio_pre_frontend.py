"""Build and embed frontend assets before PlatformIO compiles the firmware."""

import os
import shlex
import sys
from pathlib import Path

platformio_import = globals()["Import"]
platformio_import("env")
environment = globals()["env"]

root = Path(environment["PROJECT_DIR"])
frontend = root / "frontend"
embed = root / "scripts" / "embed_web_assets.py"
dist_index = frontend / "dist" / "index.html"


def _execute(command: list[str]) -> None:
    status = environment.Execute(shlex.join(command))
    if status:
        message = f"command failed with exit status {status}: {command[0]}"
        raise RuntimeError(message)


# make check builds Vite first; skip only that duplicate build when requested.
if os.environ.get("CHAYA_SKIP_FRONTEND_BUILD") == "1" and dist_index.is_file():
    print("Skipping frontend rebuild (CHAYA_SKIP_FRONTEND_BUILD=1, dist present)")
else:
    node_modules = frontend / "node_modules"
    if not node_modules.is_dir():
        print("Installing frontend dependencies…")
        _execute(["npm", "--prefix", str(frontend), "ci"])

    print("Building frontend…")
    _execute(["npm", "--prefix", str(frontend), "run", "build"])

print("Embedding SPA assets…")
_execute([sys.executable, str(embed)])
