"""Simulate FDM printability layer by layer (0.2 mm layers, 0.4 mm nozzle).

Checks floating islands, overhangs >45°, bridge spans, thin walls, bed contact,
and tip-over risk (center of mass above the first-layer footprint).

Usage: python3 scripts/case/tools/simulate_print.py out/*.stl
"""

import sys

import numpy as np
import trimesh
from shapely.geometry import Point, Polygon

LH, NOZ = 0.2, 0.4
OVH_DEG = 45
OVH_ALLOW = LH * np.tan(np.radians(OVH_DEG + 1))  # 45° rule (+1° fillet tolerance)


def layer(mesh, z):
    """Cross-section polygon at height z (even-odd for holes)."""
    sec = mesh.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
    if sec is None:
        return Polygon()
    transform = np.eye(4)
    transform[2, 3] = -z
    p2, _ = sec.to_2D(to_2D=transform, check=False)
    res = Polygon()
    for p in p2.polygons_closed:  # even-odd: holes from XOR
        if p is not None and p.is_valid and p.area > 1e-6:
            res = res.symmetric_difference(p)
    return res.buffer(0)


def parts(g):
    """Flatten a Shapely geometry into a list of polygons."""
    return list(g.geoms) if hasattr(g, "geoms") else ([g] if not g.is_empty else [])


def analyse(path):
    """Run layer-wise printability checks on an STL mesh."""
    m = trimesh.load(path)
    zs = np.arange(LH / 2, m.bounds[1, 2], LH)
    layers = [layer(m, z) for z in zs]
    rep = {
        "name": path.split("/")[-1],
        "layers": len(zs),
        "islands": [],
        "overhang": [],
        "bridges": [],
        "thin": [],
        "thin2": 0.0,
    }
    for i in range(1, len(layers)):
        cur, prev = layers[i], layers[i - 1]
        if cur.is_empty:
            continue
        for p in parts(cur):
            if p.area > 0.05 and not p.intersects(prev):
                rep["islands"].append((zs[i], p.area, p.centroid.coords[0]))
        un = cur.difference(prev.buffer(OVH_ALLOW))
        for p in parts(un):
            if p.area < 0.3:
                continue
            # Bridge: area rests on both sides -> span = shorter rectangle side
            touch = p.buffer(0.3).intersection(prev)
            rotated = p.minimum_rotated_rectangle
            if not isinstance(rotated, Polygon):
                continue
            xs, ys = rotated.exterior.coords.xy
            e = sorted([np.hypot(xs[k + 1] - xs[k], ys[k + 1] - ys[k]) for k in range(2)])
            span = e[0]
            n_supp = len(parts(touch))
            kind = (
                "bridge"
                if n_supp >= 2 or (not touch.is_empty and touch.length > 2 * e[1])
                else "overhang"
            )
            (rep["bridges"] if kind == "bridge" else rep["overhang"]).append(
                (zs[i], p.area, span, p.centroid.coords[0])
            )
        # Wall thickness
        thin = cur.difference(cur.buffer(-NOZ / 2).buffer(NOZ / 2 + 0.01))
        for p in parts(thin):
            if p.area > 0.15:
                rep["thin"].append((zs[i], p.area, p.centroid.coords[0]))
        rep["thin2"] += cur.difference(cur.buffer(-NOZ).buffer(NOZ + 0.01)).area * LH
        t12 = cur.difference(cur.buffer(-0.595).buffer(0.6))
        for p in parts(t12):
            if p.area > 0.3:
                rep.setdefault("thin12", []).append((zs[i], p.area, p.centroid.coords[0]))

    # Bed + center of mass
    base = layers[0]
    cg = m.center_mass if m.is_watertight else m.centroid
    hull = base.convex_hull
    rep["bed_area"] = base.area
    rep["cg"] = cg
    rep["cg_inside"] = hull.contains(Point(cg[0], cg[1]))
    if isinstance(hull, Polygon):
        rep["cg_margin"] = hull.exterior.distance(Point(cg[0], cg[1]))
    else:
        rep["cg_margin"] = float(hull.distance(Point(cg[0], cg[1])))
    rep["h_ratio"] = m.extents[2] / min(m.extents[0], m.extents[1])
    rep["tip_angle"] = np.degrees(np.arctan2(rep["cg_margin"], cg[2] - m.bounds[0, 2]))
    rep["extents"] = m.extents
    return rep


def fmt(rep):
    """Format an analysis report for printing."""
    ex = rep["extents"]
    header = (
        f"\n=== {rep['name']}  ({ex[0]:.1f} x {ex[1]:.1f} x {ex[2]:.1f} mm, {rep['layers']} layers)"
    )
    out = [header]

    def ok(c):
        return "OK  " if c else "WARN"

    out.append(
        f"{ok(not rep['islands'])}  Floating islands: {len(rep['islands'])}"
        + "".join(
            f"\n        z={z:.1f} {a:.2f} mm² at {c[0]:.1f},{c[1]:.1f}"
            for z, a, c in rep["islands"][:8]
        )
    )
    big = [o for o in rep["overhang"] if o[1] > 1.0]
    out.append(
        f"{ok(not big)}  Overhangs >45° (non-bridge) > 1 mm²/layer: {len(big)}"
        + "".join(
            f"\n        z={z:.1f} {a:.2f} mm², cantilever {s:.2f} mm at {c[0]:.1f},{c[1]:.1f}"
            for z, a, s, c in sorted(big, key=lambda o: -o[1])[:8]
        )
    )
    small = [o for o in rep["overhang"] if o[1] <= 1.0]
    max_small = max([o[2] for o in small], default=0)
    out.append(
        f"INFO  small overhangs (<=1 mm²/layer, non-critical): {len(small)}, "
        f"max. cantilever {max_small:.2f} mm"
    )
    mx = max([b[2] for b in rep["bridges"]], default=0)
    out.append(
        f"{ok(mx <= 10)}  Bridges: {len(rep['bridges'])} areas, "
        f"longest span {mx:.1f} mm (Bambu handles >20 mm)"
    )
    out.append(
        f"{ok(not rep['thin'])}  Spots thinner than 0.4 mm (would be skipped): {len(rep['thin'])}"
        + "".join(
            f"\n        z={z:.1f} {a:.2f} mm² at {c[0]:.1f},{c[1]:.1f}"
            for z, a, c in rep["thin"][:8]
        )
    )
    out.append(f"INFO  volume thinner than 0.8 mm (single perimeter): {rep['thin2']:.1f} mm³")
    t = rep.get("thin12", [])
    zones = {}
    for z, a, c in t:
        key = (round(c[0] / 3) * 3, round(c[1] / 3) * 3)
        zones.setdefault(key, [0, z, z])
        zones[key][0] += a * LH
        zones[key][1] = min(zones[key][1], z)
        zones[key][2] = max(zones[key][2], z)
    out.append(
        f"INFO  regions thinner than 1.2 mm: {len(zones)}"
        + "".join(
            f"\n        at x{k[0]:+.0f} y{k[1]:+.0f}, z {v[1]:.1f}-{v[2]:.1f}, {v[0]:.1f} mm³"
            for k, v in sorted(zones.items(), key=lambda kv: -kv[1][0])[:12]
        )
    )
    out.append(f"{ok(rep['bed_area'] > 60)}  First-layer bed contact: {rep['bed_area']:.0f} mm²")
    cg_ok = rep["cg_inside"]
    cg_txt = "yes" if cg_ok else "NO"
    out.append(
        f"{ok(cg_ok)}  Center of mass over footprint: {cg_txt}, "
        f"margin to tip edge {rep['cg_margin']:.1f} mm, "
        f"tip angle {rep['tip_angle']:.0f}°, height/base {rep['h_ratio']:.2f}"
    )
    return "\n".join(out)


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(fmt(analyse(p)))
