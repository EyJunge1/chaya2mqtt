"""Normalize compile_commands.json for clangd after firmware link."""

Import = globals()["Import"]
Import("env")
environment = globals()["env"]

import subprocess
import sys
from pathlib import Path


def _fix_compile_commands(source, target, env):  # noqa: ARG001
    script = Path(env["PROJECT_DIR"]) / "scripts" / "pio" / "fix_compile_commands.py"
    if not script.is_file():
        return
    subprocess.run([sys.executable, str(script)], check=False)


environment.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _fix_compile_commands)
