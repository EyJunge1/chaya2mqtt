"""Run wall_thickness on print-oriented STLs and map thin spots back to model coords."""

import battery_case_model as ec
import wall_thickness as wt

fb = ec.front_shell().val().BoundingBox()
lb = ec.lid().val().BoundingBox()


def lab_front(c):
    """Map print coordinates to front-shell model coordinates."""
    x, y, z = c[0] + fb.center.x, c[1] + fb.center.y, c[2] + fb.zmin
    return f"model x{x:.1f} y{y:.1f} z{z:.1f}"


def lab_lid(c):
    """Map print coordinates to lid model coordinates (180° flip)."""
    x = c[0] + lb.center.x
    y = lb.center.y - c[1]
    z = lb.zmax - c[2]
    return f"model x{x:.1f} y{y:.1f} z{z:.1f}"


for f, lab in [
    ("out/gehaeuse_front.stl", lab_front),
    ("out/gehaeuse_deckel.stl", lab_lid),
    ("out/tasten_2x.stl", None),
]:
    wt.report(wt.local_thickness(f), label=lab)
