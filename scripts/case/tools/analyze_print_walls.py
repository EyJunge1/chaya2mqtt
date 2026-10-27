"""Run wall_thickness on print-oriented STLs and map thin spots back to model coords.

Usage:
  python3 scripts/case/tools/analyze_print_walls.py battery
  python3 scripts/case/tools/analyze_print_walls.py no_battery
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from typing import Any

import wall_thickness as wt

CASE_ROOT = Path(__file__).resolve().parents[1]
VARIANTS = {
    "battery": ("battery", "battery_case_model.py"),
    "no_battery": ("no_battery", "no_battery_case_model.py"),
}


def load_model(variant: str) -> Any:
    """Import the CadQuery case module for a variant directory."""
    if variant not in VARIANTS:
        names = ", ".join(sorted(VARIANTS))
        message = f"unknown variant {variant!r}; expected one of: {names}"
        raise SystemExit(message)
    folder, filename = VARIANTS[variant]
    path = CASE_ROOT / folder / filename
    spec = importlib.util.spec_from_file_location(f"{variant}_case_model", path)
    if spec is None or spec.loader is None:
        message = f"cannot load case model from {path}"
        raise ImportError(message)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def main(argv: list[str] | None = None) -> int:
    """Analyze wall thickness for one case variant's print STLs."""
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) != 1:
        usage = __doc__ or "Usage: analyze_print_walls.py battery|no_battery"
        print(usage.strip(), file=sys.stderr)
        return 2
    variant = args[0]
    ec = load_model(variant)
    fb = ec.front_shell().val().BoundingBox()
    lb = ec.lid().val().BoundingBox()

    def lab_front(c: list[float] | tuple[float, ...]) -> str:
        """Map print coordinates to front-shell model coordinates."""
        x, y, z = c[0] + fb.center.x, c[1] + fb.center.y, c[2] + fb.zmin
        return f"model x{x:.1f} y{y:.1f} z{z:.1f}"

    def lab_lid(c: list[float] | tuple[float, ...]) -> str:
        """Map print coordinates to lid model coordinates (180° flip)."""
        x = c[0] + lb.center.x
        y = lb.center.y - c[1]
        z = lb.zmax - c[2]
        return f"model x{x:.1f} y{y:.1f} z{z:.1f}"

    jobs = [
        ("out/gehaeuse_front.stl", lab_front),
        ("out/gehaeuse_deckel.stl", lab_lid),
        ("out/tasten_2x.stl", None),
    ]
    for path, lab in jobs:
        wt.report(wt.local_thickness(path), label=lab)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
