Import("env")  # type: ignore  # noqa: F821 — PlatformIO injects env

import subprocess
import sys
from pathlib import Path

root = Path(env["PROJECT_DIR"])  # type: ignore  # noqa: F821
frontend = root / "frontend"
embed = root / "tools" / "embed_web_assets.py"

node_modules = frontend / "node_modules"
if not node_modules.is_dir():
    print("Installing frontend dependencies…")
    subprocess.check_call(["npm", "ci"], cwd=str(frontend))

print("Building frontend…")
subprocess.check_call(["npm", "run", "build"], cwd=str(frontend))

print("Embedding SPA assets…")
subprocess.check_call([sys.executable, str(embed)], cwd=str(root))
