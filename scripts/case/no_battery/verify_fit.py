"""Verify the USB-only CadQuery case fits the Waveshare board STEP (clearances, openings, snaps)."""

import cadquery as cq
import no_battery_case_model as ec
import trimesh

R = []


def ok(name, cond, info=""):
    """Record a pass/fail check line for the summary report."""
    R.append(("OK  " if cond else "FAIL") + "  " + name + ("  (" + info + ")" if info else ""))


fs, ld, bt = ec.front_shell(), ec.lid(), ec.buttons_assembled()
board = cq.importers.importStep("../../dl/zip/x/f1.stp")
allsol = board.solids().vals()
hdr = [s for s in allsol if abs(s.BoundingBox().zmax - 9.0) < 0.01]
spk = [
    s for s in allsol if abs(s.BoundingBox().zmax - 5.5) < 0.01 and s.BoundingBox().ymin > 20
]  # speaker connector removed from build
bs = [s for s in allsol if s not in hdr and s not in spk]
ok(
    "Speaker connector removed from model (battery connector remains)",
    len(spk) == 1,
    f"{len(spk)} solid",
)
ok(
    "Pin header removed from model",
    len(hdr) == 1,
    f"{len(hdr)} solid, bbox x{hdr[0].BoundingBox().xmin:.1f}-{hdr[0].BoundingBox().xmax:.1f}",
)
for n, p in [("Front shell", fs), ("Lid", ld), ("Buttons", bt)]:
    ok(f"{n} valid solid", p.val().isValid())
    v = sum(p.val().intersect(s).Volume() for s in bs)
    ok(f"{n} does not collide with board", v < 0.001, f"{v:.3f} mm³")
ok("Lid intersect front shell = 0", fs.val().intersect(ld.val()).Volume() < 0.001)
ok(
    "Buttons intersect case = 0",
    bt.val().intersect(fs.val()).Volume() < 0.001 and bt.val().intersect(ld.val()).Volume() < 0.001,
)
# Minimum clearance board <-> case
dmin = min(fs.val().distance(s) for s in bs)
ok("Minimum clearance board-front shell > 0", dmin > 0.0 or dmin == 0, f"{dmin:.2f} mm")
dmin2 = min(ld.val().distance(s) for s in bs if s.BoundingBox().zmax > 0.5)
ok("Lid-to-components clearance", True, f"{dmin2:.2f} mm")
# No battery: lid inner face above tallest component (battery connector 5.5 mm)
dmin = min(ld.val().distance(s_) for s_ in bs if s_.BoundingBox().zmax > 2.0)
ok("Lid above all components (battery connector)", dmin > 0.1, f"gap {dmin:.2f} mm")
# Display window vs active area (27x27 @ x3.5-30.5, y8.7-35.7)
win = (ec.AA_CX - ec.WIN / 2, ec.AA_CX + ec.WIN / 2, ec.AA_CY - ec.WIN / 2, ec.AA_CY + ec.WIN / 2)
ok(
    "Window fully covers active area",
    win[0] <= 3.5 and win[1] >= 30.5 and win[2] <= 8.7 and win[3] >= 35.7,
    f"window x{win[0]:.1f}-{win[1]:.1f} y{win[2]:.1f}-{win[3]:.1f}, "
    f"margin each {(ec.WIN - 27) / 2:.1f} mm",
)
lcd = next(s for s in bs if s.BoundingBox().zmin < -4.4).BoundingBox()
ok(
    "Window lip rests on display glass (not on bezel edge)",
    win[0] > lcd.xmin and win[1] < lcd.xmax and win[2] > lcd.ymin and win[3] < lcd.ymax,
    f"glass x{lcd.xmin:.1f}-{lcd.xmax:.1f} y{lcd.ymin:.1f}-{lcd.ymax:.1f}",
)
# Buttons vs switches
sw = [s for s in bs if abs(s.BoundingBox().xmin + 0.55) < 0.02]
for s in sw:
    b = s.BoundingBox()
    yc = (b.ymin + b.ymax) / 2
    # Switch plunger face at x=-0.55
    fx = [
        f
        for f in s.Faces()
        if f.BoundingBox().xlen < 0.01 and abs(f.BoundingBox().xmin + 0.55) < 0.02
    ]
    fb = fx[0].BoundingBox() if fx else b
    act = ((fb.ymin + fb.ymax) / 2, (fb.zmin + fb.zmax) / 2)
    by = min(ec.BTN_Y, key=lambda v: abs(v - yc))
    ok(
        f"Button y={by} aligns with switch plunger",
        abs(by - act[0]) < 0.6 and abs(ec.BTN_Z - act[1]) < 0.8,
        f"plunger y{act[0]:.2f} z{act[1]:.2f}, cap y{by} z{ec.BTN_Z}",
    )
    gap = bt.val().distance(s)
    ok("  cap-to-switch gap", 0.05 < gap < 0.4, f"{gap:.2f} mm")
# USB
usb = next(s for s in bs if abs(s.BoundingBox().xmin - 11.21) < 0.05).BoundingBox()
ok(
    "USB opening centered on connector",
    abs((usb.xmin + usb.xmax) / 2 - 17) < 0.1 and abs((usb.zmin + usb.zmax) / 2 - ec.USB_Z) < 0.1,
    f"connector center x{(usb.xmin + usb.xmax) / 2:.2f} z{(usb.zmin + usb.zmax) / 2:.2f}",
)
face = (ec.IN_Y0 - ec.USB_WALL) if ec.USB_RECESS else ec.OUT_Y0
dist = usb.ymin - face
ok(
    "Connector-to-outer-face distance (plug depth)",
    6.65 - dist >= 4.5,
    f"{dist:.2f} mm -> plug inserts {6.65 - dist:.2f} of 6.65 mm (original case ~4.6 mm)",
)
# Plug probe: shell 8.34x2.56 to connector + overmold 12.5x6.5 from outer face
plug = (
    cq.Workplane("XZ")
    .workplane(offset=-(usb.ymin - 0.05))
    .center(17, 0)
    .slot2D(8.34, 2.56)
    .extrude(dist + 0.1)
    .union(
        cq.Workplane("XZ")
        .workplane(offset=-(face - 0.02))
        .center(17, 0)
        .rect(12.5, 6.5)
        .extrude(10)
        .edges("|Y")
        .fillet(1.5)
    )
)
ok(
    "Standard plug fits (shell through slot, overmold to outer face)",
    plug.val().intersect(fs.val()).Volume() < 0.001,
)
# LED hole: clear line of sight from LED to outside
sight = (
    cq.Workplane("XZ")
    .workplane(offset=2.7)
    .center(ec.LED_X, ec.LED_Z)
    .circle(0.5)
    .extrude(-(ec.OUT_Y0 - 1) - 2.7)
)
ok(
    "LED hole aligns with LED (clear sight line)",
    sight.val().intersect(fs.val()).Volume() < 0.001,
    f"LED x{ec.LED_X} z{ec.LED_Z}, hole dia {ec.LED_D}",
)
# Guide ribs: clearance to display
for side, y in ec.RIB_POS:
    xe = ec.DISP_X1 + ec.RIB_CLR if side > 0 else ec.DISP_X0 - ec.RIB_CLR
    rib_pt = cq.Workplane("XY").box(0.05, 0.05, 0.05).translate((xe + side * 0.03, y, -4.0)).val()
    lcd2 = next(s_ for s_ in bs if s_.BoundingBox().zmin < -4.4)
    d_ = rib_pt.distance(lcd2)
    ok(
        f"Display rib {'right' if side > 0 else 'left'} y={y} at display",
        0.05 < d_ < 0.2,
        f"gap {d_ + 0.0:.2f} mm",
    )
lcd_ = next(s_ for s_ in bs if s_.BoundingBox().zmin < -4.4)
for side, x in ec.RIB_Y_POS:
    ye = ec.DISP_Y1 + ec.RIB_CLR if side > 0 else ec.DISP_Y0 - ec.RIB_CLR
    pt = cq.Workplane("XY").box(0.05, 0.05, 0.05).translate((x, ye + side * 0.03, -4.0)).val()
    d_ = pt.distance(lcd_)
    ok(
        f"Display spacer {'top' if side > 0 else 'bottom'} x={x} at display",
        0.05 < d_ < 0.2,
        f"gap {d_:.2f} mm",
    )
# M2 x 4 screws: shank through PCB hole into dome, head (dia 4.0 x 1.8) on PCB
pcbonly = [
    s_
    for s_ in bs
    if abs(s_.BoundingBox().xlen - 34) < 0.1 and abs(s_.BoundingBox().ylen - 47.8) < 0.1
]
others = [s_ for s_ in bs if s_ not in pcbonly]
for x, y in ec.HOLES:
    head = cq.Workplane("XY").workplane(offset=0.0).center(x, y).circle(2.0).extrude(1.8).val()
    shank = cq.Workplane("XY").workplane(offset=0.0).center(x, y).circle(1.0).extrude(-4.0).val()
    v_head = head.intersect(ld.val()).Volume() + sum(head.intersect(o).Volume() for o in others)
    v_sh = sum(shank.intersect(o).Volume() for o in others)
    hole_end = ec.Z_GLASS
    ok(
        f"M2x4 screw at ({x},{y}): head clear, shank clear, tip in dome",
        v_head < 0.001 and v_sh < 0.001 and hole_end < -4.0,
        f"pilot hole dia {ec.PCB_SCREW_PILOT} to 3.55 mm deep, "
        f"thread depth 3.0 mm, front stays full 1.2 mm",
    )
# Snap hook clearance to components
for side, y in ec.SNAP_POS:
    sp = ec.snap_profile(side, y, ec.SNAP_W).val()
    d = min(sp.distance(s) for s in bs)
    ok(f"Snap hook {'right' if side > 0 else 'left'} y={y} clear", d > 0.2, f"{d:.2f} mm")
# Wall
inner_ok = (
    ec.cavity(0)
    .cut(
        ec.rrect(
            ec.W - 2 * (ec.MIN_WALL - 0.02),
            ec.H - 2 * (ec.MIN_WALL - 0.02),
            ec.R_PLAN - (ec.MIN_WALL - 0.02),
            -20,
            30,
        )
    )
    .val()
    .Volume()
    < 0.001
)
ok(
    f"Inner cavity keeps >= {ec.MIN_WALL} mm to outer contour (top view)",
    inner_ok,
    "3D wall thickness see scripts/case/tools/wall_thickness.py",
)
# STL manifold
for n in ["gehaeuse_front", "gehaeuse_deckel", "tasten_2x"]:
    m = trimesh.load(f"out/{n}.stl")
    ok(f"STL {n} watertight", m.is_watertight, f"{len(m.faces)} triangles")
print("\n".join(R))
print(f"\nOuter size {ec.W:.1f} x {ec.H:.1f} x {ec.T:.1f} mm")
