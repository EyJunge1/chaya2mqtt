"""Measure true 3D wall thickness (voxel + distance transform, BoneJ-style local thickness).

Thickness at a point = diameter of the largest sphere that fits in the material there.
Evaluated on the medial ridge; corner-only ridges are ignored.

Usage: python3 scripts/case/battery/wall_thickness.py out/*.stl
"""

import sys

import numpy as np
import trimesh
from matplotlib.path import Path
from scipy import ndimage

PITCH = 0.1


def local_thickness(path):
    """Voxelize mesh and estimate local wall thickness on the medial ridge."""
    m = trimesh.load(path)
    lo = m.bounds[0] - 2 * PITCH
    shape = np.ceil((m.bounds[1] - lo) / PITCH).astype(int) + 2
    # Point grid -> inside/outside via ray test (robust, watertight mesh)
    vox = np.zeros(shape, bool)
    xs = lo[0] + (np.arange(shape[0]) + 0.5) * PITCH
    ys = lo[1] + (np.arange(shape[1]) + 0.5) * PITCH
    zs = lo[2] + (np.arange(shape[2]) + 0.5) * PITCH
    # Layer-wise polygons (fast): cross-section per z, rasterize via matplotlib Path
    grid_x, grid_y = np.meshgrid(xs, ys, indexing="ij")
    points = np.c_[grid_x.ravel(), grid_y.ravel()]
    for k, z in enumerate(zs):
        sec = m.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
        if sec is None:
            continue
        inside = np.zeros(len(points), bool)
        for ent in sec.discrete:
            pth = Path(ent[:, :2])
            inside ^= pth.contains_points(points)
        vox[:, :, k] = inside.reshape(grid_x.shape)
    edt = ndimage.distance_transform_edt(vox) * PITCH
    mx = ndimage.maximum_filter(edt, size=9)
    ridge = vox & (edt >= mx - 0.5 * PITCH) & (edt > 0)
    # Ridge voxels whose neighborhood (radius ~ local thickness) has a much larger value
    # are corner spurs -> ignore
    mx2 = ndimage.maximum_filter(edt, size=15)
    ridge &= edt >= mx2 - 1.5 * PITCH
    t = 2 * edt[ridge]
    idx = np.argwhere(ridge)
    pos = np.c_[xs[idx[:, 0]], ys[idx[:, 1]], zs[idx[:, 2]]]
    return {
        "name": path.split("/")[-1],
        "tmin": t.min(),
        "tmax": 2 * edt.max(),
        "t": t,
        "pos": pos,
        "vol": vox.sum() * PITCH**3,
    }


def report(r, thin=1.19, thick=6.0, label=None):
    """Print wall-thickness summary; optional label maps coords to model space."""
    t, pos = r["t"], r["pos"]
    print(f"\n=== {r['name']}  (volume {r['vol']:.0f} mm³)")
    med = np.median(t)
    print(
        f"    wall thickness: min {r['tmin']:.2f} mm, max {r['tmax']:.2f} mm, median {med:.2f} mm"
    )
    sel = t < thin
    if sel.any():
        # group by location
        keys = np.round(pos[sel] / 4) * 4
        uniq, inv = np.unique(keys, axis=0, return_inverse=True)
        rows = []
        for u in range(len(uniq)):
            g = inv.ravel() == u
            rows.append((g.sum(), t[sel][g].min(), pos[sel][g].mean(0)))
        rows.sort(key=lambda r_: -r_[0])
        print(f"    below {thin + 0.01:.1f} mm at {len(rows)} locations:")
        for n, tm, c in rows[:14]:
            lab = f"  -> {label(c)}" if label else ""
            print(
                f"      {tm:.2f} mm  (print coords x{c[0]:+.1f} y{c[1]:+.1f} "
                f"z{c[2]:.1f}, {n} points){lab}"
            )
    else:
        print(f"    nowhere below {thin + 0.01:.1f} mm")
    status = "OK  " if r["tmax"] <= thick else "WARN"
    print(f"    {status} thickest spot {r['tmax']:.2f} mm (limit {thick} mm)")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        report(local_thickness(p))
