Import("env")  # type: ignore  # noqa: F821 — PlatformIO injects env

import os
import subprocess
import sys
from pathlib import Path

root = Path(env["PROJECT_DIR"])  # type: ignore  # noqa: F821
frontend = root / "frontend"
embed = root / "scripts" / "embed_web_assets.py"
blob = root / "src" / "web" / "assets" / "web_ui.bin"

# make check builds + embeds first; skip a duplicate Vite build when requested.
if os.environ.get("CHAYA_SKIP_FRONTEND_BUILD") == "1" and blob.is_file():
    print("Skipping frontend rebuild (CHAYA_SKIP_FRONTEND_BUILD=1, web_ui.bin present)")
else:
    node_modules = frontend / "node_modules"
    if not node_modules.is_dir():
        print("Installing frontend dependencies…")
        subprocess.check_call(["npm", "ci"], cwd=str(frontend))

    print("Building frontend…")
    subprocess.check_call(["npm", "run", "build"], cwd=str(frontend))

    print("Embedding SPA assets…")
    subprocess.check_call([sys.executable, str(embed)], cwd=str(root))
