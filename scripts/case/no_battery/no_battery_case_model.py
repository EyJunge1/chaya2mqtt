"""
USB-powered slim case (no battery) for Waveshare ESP32-S3-ePaper-1.54(G).

Same geometry as scripts/case/battery/, only thinner:
  Z_LID_IN = MX1.25 battery connector (5.5 mm) + clearance — no 602530 cell.

Variant: scripts/case/no_battery/
Shared tools: scripts/case/tools/
Sibling: scripts/case/battery/

- Rear 2x6 pin header desoldered; battery connector remains
- Rear cover as snap cap, screwless: 4 snap hooks + bottom hanging tab
- Openings: USB-C bottom, BOOT/PWR right (separate button caps)
- Speaker + speaker connector removed
- LED hole bottom, guide ribs left/right for PCB/display, no battery frame
- PCB screwed to domes with 4x M2 x 4 self-tapping screws
- FDM optimized: walls >= 1.2 mm, overhangs <= 45°, hole ceilings teardrop/45°,
  notches at hook roots softened, 45° chamfers on bed-facing edges
CadQuery 2.x - `python3 scripts/case/no_battery/no_battery_case_model.py`
  writes print-ready STLs to ./out (lid outer face on the bed).

Coordinates = Waveshare STEP (component side +z):
  PCB x 0..34, y -4..43.8, z -1..0 | Display glass z -4.5 | USB-C bottom x=17, z=0
"""

import contextlib
import math
import os

import cadquery as cq
import trimesh

# ------------------------------------------------------------------ Parameters
WALL_SIDE, WALL_FRONT, LID_T = 1.6, 1.2, 1.4
R_PLAN = 9.0  # large, soft corners top + bottom (Pala style)
MIN_WALL = 1.2  # minimum wall at corners (inner cavity follows outer rounding there)
EXTRA = 1.5  # extra wall so large corners do not clip the PCB
R_FRONT = 3.5  # soft rounded transition front -> side
R_BACK = 3.0  # soft rounded transition back -> side
R_IN = 2.9  # inner corners
# inner fillet front slightly larger than concentric -> >= 1.2 mm wall everywhere
R_IN_F = R_FRONT - 1.1

# 602530 cell (Pala note parts list) - measure to confirm
BAT_W, BAT_L, BAT_T = 25.0, 30.0, 6.0
BAT_CX, BAT_CY = 16.4, 15.5  # clear of buttons, connectors, and coil
BAT_CLR = 0.5
BAT_Z0 = 3.1  # cell bottom edge (components max 2.8 mm)

# Optional openings (positions from Waveshare STEP / BOM)
MIC_HOLE = False  # microphone (bottom, x 26.3) - needed for voice capture
SHTC3_VENT = False  # temp/humidity sensor (bottom, x 7.6) - needed for usable readings
SPEAKER_GRILLE = False  # speaker removed -> closed on top
LED_HOLE = True  # hole for LED (LED_G on GP3) bottom, like original case
LED_X, LED_Z, LED_D = (
    26.3,
    -1.25,
    1.5,
)  # LED on front of PCB at lower edge (Waveshare STEP)
# Guide ribs left/right: hold PCB + display laterally in exact position
RIB_POS = [
    (-1, 26.0),
    (-1, 36.0),
    (1, 26.0),
    (1, 36.0),
]  # (side, y), clearance from 6-pin flat cable connector (y 14-20)
RIB_W, RIB_CLR = 1.2, 0.1
# Top/bottom spacers for display (under PCB, z < -1): (side, x)
RIB_Y_POS = [(-1, 7.0), (-1, 27.5), (1, 12.0), (1, 22.0)]
DISP_Y0, DISP_Y1 = 0.78, 38.1  # display glass edge bottom/top (Waveshare STEP)
DISP_X0, DISP_X1 = 1.1, 32.9  # display glass edge left/right (Waveshare STEP)
PRINT_45 = True  # 45° chamfer on bed-facing edges (front + lid)
USB_RECESS = False  # like original: plain slot, no recess
USB_FACE_DIST = 2.0  # connector -> outer face like Waveshare case
USB_SLOT_W, USB_SLOT_H, USB_CHAMF = 9.6, 3.8, 0.5
USB_REC_W, USB_REC_H = 13.6, 7.6  # room for cable overmold (standard cable)
USB_WALL = 1.0  # remaining wall in front of connector -> plug inserts 5.5 of 6.65 mm
SCREW_HOLES = []  # lid: no screws, snap fit
PCB_SCREW_PILOT = 1.7  # pilot hole in domes for M2 x 4 self-tapping (PCB is screwed on)
HOLES = [(3.0, -2.3), (31.0, -2.3), (3.0, 41.7), (31.0, 41.7)]
TAB_W = 14.0
# Snap hooks on lid (2 each left/right) + bottom hanging tab
SNAP_Y = (24.0, 35.5)  # slots stay on straight lip (not in corner fillet)
SNAP_W = 7.0
SNAP_Z0 = 0.5  # hook lower edge
SNAP_BUMP = 0.45  # latch bump outward
SNAP_SLOT = 0.8
SNAP_T = 1.2  # hook thickness (minimum wall 1.2)
LIP_T = 1.2  # lid lip wall thickness
FRAME_T = 1.2  # battery frame wall thickness
ROOT_CH = 0.4  # 45° reinforcement at hook/lip root (reduce stress concentration)
RIM_CH = 0.5  # chamfer on front shell inner rim (lead-in + room for ROOT_CH)
TAB_DROP = 0.8  # hanging tab set lower -> enough material above pocket

# ------------------------------------------------------------------ derived
IN_X0, IN_X1 = -1.6, 35.6
IN_Y0, IN_Y1 = -4.15, 43.8 + 0.15  # top: speaker removed -> inner cavity ends at PCB
Z_GLASS = -4.55
Z_LID_IN = 5.5 + 0.4  # MX1.25 battery connector + clearance; no cell
Z_FRONT, Z_BACK = Z_GLASS - WALL_FRONT, Z_LID_IN + LID_T
Z_SPLIT = Z_BACK - 3.5
LIP_H = 2.5
OUT_X0, OUT_X1 = IN_X0 - WALL_SIDE - EXTRA, IN_X1 + WALL_SIDE + EXTRA
OUT_Y0, OUT_Y1 = (
    -4.0 - USB_FACE_DIST,
    43.8 + USB_FACE_DIST,
)  # top and bottom 2 mm PCB edge -> outer
CX, CY = (OUT_X0 + OUT_X1) / 2, (OUT_Y0 + OUT_Y1) / 2
W, H, T = OUT_X1 - OUT_X0, OUT_Y1 - OUT_Y0, Z_BACK - Z_FRONT
AA_CX, AA_CY, WIN, WIN_R = 17.0, 22.2, 28.0, 1.5
USB_Z = 0.0


def rrect(w, h, r, z0, z1, cx=CX, cy=CY):
    """Rounded-rect extrusion between z0 and z1."""
    return (
        cq.Workplane("XY")
        .workplane(offset=z0)
        .center(cx, cy)
        .rect(w, h)
        .extrude(z1 - z0)
        .edges("|Z")
        .fillet(r)
    )


def outer_body(c=0.0):
    """Outer shell solid with optional clearance offset c."""
    b = rrect(W + 2 * c, H + 2 * c, R_PLAN + c, Z_FRONT - c, Z_BACK + c)
    b = b.faces("<Z").edges().fillet(R_FRONT + c).faces(">Z").edges().fillet(R_BACK + c)
    if PRINT_45:
        # Print optimization "teardrop": flat part of rounding at the print bed replaced by
        # tangent 45° chamfer -> no steep overhang at front and lid edges
        k = 1 - 0.5**0.5
        for r, z_face, sgn in ((R_FRONT + c, Z_FRONT - c, 1), (R_BACK + c, Z_BACK + c, -1)):
            h = r * k  # height of 45° tangent point
            cb = rrect(W + 2 * c, H + 2 * c, R_PLAN + c, Z_FRONT - c, Z_BACK + c)
            cb = cb.faces("<Z" if sgn > 0 else ">Z").chamfer(2 * h - 0.01)
            zs = (z_face, z_face + h) if sgn > 0 else (z_face - h, z_face)
            b = b.union(cb.intersect(slab(*zs)))
    return b


def cavity(off=0.0, z0=Z_GLASS, z1=Z_BACK + 2, front_round=True):
    """Inner cavity solid with minimum wall thickness."""
    c = rrect(
        IN_X1 - IN_X0 - 2 * off,
        IN_Y1 - IN_Y0 - 2 * off,
        R_IN - off,
        z0,
        z1,
        cx=(IN_X0 + IN_X1) / 2,
        cy=(IN_Y0 + IN_Y1) / 2,
    )
    d = MIN_WALL + off  # inner cavity keeps MIN_WALL to outer contour everywhere
    c = c.intersect(rrect(W - 2 * d, H - 2 * d, R_PLAN - d, z0 - 1, z1 + 1))
    with contextlib.suppress(Exception):  # kink where corner fillet cuts the wall
        c = c.edges("|Z").edges(cq.selectors.TypeSelector("LINE")).fillet(max(0.3, 3.0 - off))
    if front_round:
        c = c.faces("<Z").edges().fillet(R_IN_F)
    return c


def slab(z0, z1):
    """Large XY clipping slab between z0 and z1."""
    return cq.Workplane("XY").box(200, 200, z1 - z0).translate((CX, CY, (z0 + z1) / 2))


def rr_wire(w, h, r, z):
    """Return rounded rectangle wire at height z (loft profile)."""
    return (
        cq.Workplane("XY")
        .rect(w, h)
        .extrude(1)
        .edges("|Z")
        .fillet(r)
        .faces("<Z")
        .wires()
        .val()
        .translate(cq.Vector(AA_CX, AA_CY, z))
    )


def rr_wire_x(w, h, r, x, y, z):
    """Return rounded rectangle in YZ plane at x (chamfers on side holes)."""
    return (
        cq.Workplane("YZ")
        .rect(w, h)
        .extrude(0.1)
        .edges("|X")
        .fillet(r)
        .faces("<X")
        .wires()
        .val()
        .translate(cq.Vector(x, y, z))
    )


def snap_profile(side, y, width, grow=0.0):
    """Latch bump (side=+1 right, -1 left): lead-in chamfer bottom, 45° top.

    Printable without support in both parts and releasable for opening.
    """
    x0 = (IN_X1 - 0.15) if side > 0 else (IN_X0 + 0.15)
    b, z = SNAP_BUMP + grow, SNAP_Z0 + 0.2
    pts = [(-0.3, z - grow), (b, z + 0.8), (b, z + 1.0), (-0.3, z + 1.0 + (b + 0.3) + grow)]
    return (
        cq.Workplane("XZ")
        .workplane(offset=-(y + width / 2))
        .polyline([(x0 + side * px, pz) for px, pz in pts])
        .close()
        .extrude(width)
    )


# ------------------------------------------------------------------ Front shell
def front_shell():
    """Front shell with display window, ribs, USB, and button openings."""
    s = outer_body().intersect(slab(Z_FRONT - 1, Z_SPLIT)).cut(cavity())

    # Display window: rounded, 45° chamfer outward, 0.4 mm straight land inside (no knife edge)
    land = 0.4
    d = WALL_FRONT - land + 0.1
    win = cq.Solid.makeLoft(
        [
            rr_wire(WIN, WIN, WIN_R, Z_GLASS - land),
            rr_wire(WIN + 2 * d, WIN + 2 * d, WIN_R + d, Z_GLASS - land - d),
        ],
        True,
    )
    s = s.cut(cq.Workplane().add(win))
    s = s.cut(
        cq.Workplane().add(
            cq.Solid.makeLoft(
                [
                    rr_wire(WIN, WIN, WIN_R, Z_GLASS + 0.1),
                    rr_wire(WIN, WIN, WIN_R, Z_GLASS - land - 0.01),
                ],
                True,
            )
        )
    )

    # Screw domes: PCB screwed from rear with 4x M2 x 4 (self-tapping)
    for x, y in HOLES:
        s = s.union(
            cq.Workplane("XY")
            .workplane(offset=Z_GLASS - 0.01)
            .center(x, y)
            .circle(2.2)
            .extrude(-1.0 - Z_GLASS)
        )
        s = s.cut(
            cq.Workplane("XY")
            .workplane(offset=-0.9)
            .center(x, y)
            .circle(PCB_SCREW_PILOT / 2)
            .extrude(Z_GLASS - (-0.9))
        )  # only in dome, front stays full 1.2 mm
        s = s.cut(
            cq.Workplane("XY")
            .workplane(offset=-0.99)
            .center(x, y)  # countersink for seating
            .circle(PCB_SCREW_PILOT / 2 + 0.3)
            .workplane(offset=-0.3)
            .circle(PCB_SCREW_PILOT / 2)
            .loft()
        )

    # Snap grooves for hooks
    for side in (-1, 1):
        for y in SNAP_Y:
            s = s.cut(snap_profile(side, y, SNAP_W + 1.0, grow=0.1))

    # Pocket for hanging tab: ceiling rises outward at 45° (no horizontal overhang),
    # tab latches on edge to inner wall
    z0 = Z_SPLIT - LIP_H - TAB_DROP
    pk = [
        (IN_Y0 + 0.5, z0),
        (IN_Y0 - 0.75, z0),
        (IN_Y0 - 0.75, z0 + 1.85),
        (IN_Y0 - 0.5, z0 + 1.6),
        (IN_Y0 + 0.5, z0 + 1.6),
    ]  # 0.5 land start, then 45°; ~1.1 mm remaining wall
    s = s.cut(
        cq.Workplane("YZ")
        .workplane(offset=17.0 - (TAB_W + 0.6) / 2)
        .polyline(pk)
        .close()
        .extrude(TAB_W + 0.6)
    )

    # Chamfer on inner rim edge (lead-in, room for root reinforcement on lid)
    s = s.cut(
        cavity(-RIM_CH, Z_SPLIT - RIM_CH, Z_SPLIT + 1, False).faces("<Z").chamfer(RIM_CH - 0.001)
    )

    # Optional openings bottom (mic, SHTC3) and top (speaker)
    thick = IN_Y0 - OUT_Y0 + 2
    if MIC_HOLE:
        s = s.cut(
            cq.Workplane("XZ")
            .workplane(offset=-(OUT_Y0 - 1))
            .center(26.3, 0.5)
            .circle(0.75)
            .extrude(-thick)
        )
    if SHTC3_VENT:
        for dx in (-1.2, 1.2):
            s = s.cut(
                cq.Workplane("XZ")
                .workplane(offset=-(OUT_Y0 - 1))
                .center(7.6 + dx, 0.6)
                .slot2D(2.6, 1.0, angle=90)
                .extrude(-thick)
            )
    if SPEAKER_GRILLE:
        for i in range(5):
            s = s.cut(
                cq.Workplane("XZ")
                .workplane(offset=-(IN_Y1 - 0.5))
                .center(12.0 + i * 2.5, 2.0)
                .slot2D(3.4, 1.0, angle=90)
                .extrude(-(OUT_Y1 - IN_Y1 + 1.5))
            )

    # Guide ribs left/right: display only (under PCB, z < -1.15),
    # to 0.1 mm from display glass (x 1.1 / 32.9), 45° lead-in on top.
    # PCB held by screws.
    for side, y in RIB_POS:
        xw = IN_X1 + 0.2 if side > 0 else IN_X0 - 0.2  # 0.2 into wall
        xd = DISP_X1 + RIB_CLR if side > 0 else DISP_X0 - RIB_CLR  # display edge
        k, zs = 0.4, -1.15
        prof = [
            (xw, Z_GLASS - 0.01),
            (xd, Z_GLASS - 0.01),
            (xd, zs - k),
            (xd + side * k, zs),
            (xw, zs),
        ]
        s = s.union(
            cq.Workplane("XZ")
            .workplane(offset=-(y + RIB_W / 2))
            .polyline(prof)
            .close()
            .extrude(RIB_W)
        )

    # Top/bottom spacers: from wall to 0.1 mm from display, 45° lead-in on top
    for side, x in RIB_Y_POS:
        yw = IN_Y1 + 0.2 if side > 0 else IN_Y0 - 0.2
        ye = DISP_Y1 + RIB_CLR if side > 0 else DISP_Y0 - RIB_CLR
        zt = -2.0 if side < 0 else -1.6  # under LED/USB port or under PCB
        k = 0.4
        prof = [
            (yw, Z_GLASS - 0.01),
            (ye, Z_GLASS - 0.01),
            (ye, zt - k),
            (ye + side * k, zt),
            (yw, zt),
        ]
        s = s.union(
            cq.Workplane("YZ").workplane(offset=x - RIB_W / 2).polyline(prof).close().extrude(RIB_W)
        )

    # LED hole bottom: round outside, teardrop behind (no horizontal ceiling)
    if LED_HOLE:
        s = s.cut(
            cq.Workplane("XZ")
            .workplane(offset=-(OUT_Y0 - 1))
            .center(LED_X, LED_Z)
            .circle(LED_D / 2)
            .extrude(-(IN_Y0 + 1 - (OUT_Y0 - 1)))
        )
        r_ = LED_D / 2
        k = r_ / 2**0.5
        tri = [(LED_X - k, LED_Z + k), (LED_X + k, LED_Z + k), (LED_X, LED_Z + 2 * k)]
        s = s.cut(
            cq.Workplane("XZ")
            .workplane(offset=-(OUT_Y0 + 0.8))
            .polyline(tri)
            .close()
            .extrude(-(IN_Y0 + 1 - (OUT_Y0 + 0.8)))
        )

    # USB-C: plain slot like original, 0.5 mm chamfer outside
    s = s.cut(
        cq.Workplane("XZ")
        .workplane(offset=-(OUT_Y0 - 1))
        .center(17, USB_Z)
        .slot2D(USB_SLOT_W, USB_SLOT_H)
        .extrude(-(IN_Y0 + 1 - (OUT_Y0 - 1)))
    )
    s = s.cut(
        cq.Workplane("XZ")
        .workplane(offset=-(OUT_Y0 - 0.01))
        .center(17, USB_Z)
        .slot2D(USB_SLOT_W + 2 * USB_CHAMF + 0.02, USB_SLOT_H + 2 * USB_CHAMF + 0.02)
        .workplane(offset=-(USB_CHAMF + 0.01))
        .slot2D(USB_SLOT_W, USB_SLOT_H)
        .loft()
    )
    if USB_RECESS:
        s = s.cut(
            cq.Workplane("XZ")
            .workplane(offset=-(OUT_Y0 - 1))
            .center(17, USB_Z)
            .rect(USB_REC_W, USB_REC_H)
            .extrude(-(1.0 + (IN_Y0 - OUT_Y0) - USB_WALL))
            .edges("|Y")
            .fillet(2.0)
        )

    # Separate button caps BOOT (y 5.5) + PWR (y 16.0): hole + guide recess inside
    a, b_, r = BTN_L / 2 + BTN_CLR, BTN_H / 2 + BTN_CLR, BTN_R + BTN_CLR
    for y in BTN_Y:
        # visible outside: rounded rectangle
        s = s.cut(
            cq.Workplane("YZ")
            .workplane(offset=OUT_X0 - 1)
            .center(y, BTN_Z)
            .rect(2 * a, 2 * b_)
            .extrude(WALL_SIDE + EXTRA + 2)
            .edges("|X")
            .fillet(r)
        )
        # teardrop behind: 45° roof tangent to upper corner radii -> no horizontal ceiling
        k = r / 2**0.5
        tx, tz = a - r + k, b_ - r + k
        roof = [(y - tx, BTN_Z + tz), (y + tx, BTN_Z + tz), (y, BTN_Z + tz + tx)]
        s = s.cut(
            cq.Workplane("YZ")
            .workplane(offset=OUT_X0 + 1.2)
            .polyline(roof)
            .close()
            .extrude(IN_X0 - OUT_X0)
        )
        # 45° chamfer on inner hole edge: room for groove between cap and flange
        xf = IN_X0 - FL_RECESS
        s = s.cut(
            cq.Workplane().add(
                cq.Solid.makeLoft(
                    [
                        rr_wire_x(2 * a + 1.0, 2 * b_ + 1.0, r + 0.5, xf + 0.01, y, BTN_Z),
                        rr_wire_x(2 * a, 2 * b_, r, xf - 0.5, y, BTN_Z),
                    ],
                    True,
                )
            )
        )
        # Inner flange recess, 45° roof
        zb, zt = BTN_Z - (FL_H + 0.4) / 2, BTN_Z + (FL_H + 0.4) / 2
        fm = [
            (IN_X0 + 0.5, zb),
            (IN_X0 - FL_RECESS, zb),
            (IN_X0 - FL_RECESS, zt),
            (IN_X0, zt + FL_RECESS),
            (IN_X0 + 0.5, zt + FL_RECESS),
        ]
        s = s.cut(
            cq.Workplane("XZ")
            .workplane(offset=-(y + (FL_L + 0.4) / 2))
            .polyline(fm)
            .close()
            .extrude(FL_L + 0.4)
        )
    return s


# ------------------------------------------------------------------ Button caps
BTN_Y, BTN_Z = (5.5, 16.0), 1.65  # switch plunger center per Waveshare STEP (x = -0.55)
BTN_L, BTN_H, BTN_CLR = 5.0, 4.0, 0.25  # rectangular, R1 corners; 0.25 clearance (holes shrink)
BTN_R = 1.0
BTN_PROUD = 0.6  # how far cap protrudes
FL_L, FL_H = 7.0, 5.8  # inner flange (prevents falling out, presses switch)
FL_RECESS = 0.4  # recess in inner wall guides flange
NUB_GAP = 0.15  # gap flange <-> switch
FL_T = (-0.55 - NUB_GAP) - (IN_X0 - FL_RECESS)  # = 1.3 mm: flange face presses directly


def button(y):
    """Single BOOT/PWR button cap with internal flange."""
    x_fl0 = IN_X0 - FL_RECESS
    head = (
        cq.Workplane("YZ")
        .workplane(offset=OUT_X0 - BTN_PROUD)
        .center(y, BTN_Z)
        .rect(BTN_L, BTN_H)
        .extrude(x_fl0 - (OUT_X0 - BTN_PROUD) + 0.01)
        .edges("|X")
        .fillet(BTN_R)
        .faces("<X")
        .edges()
        .fillet(0.5)
    )
    fl = (
        cq.Workplane("YZ")
        .workplane(offset=x_fl0)
        .center(y, BTN_Z)
        .rect(FL_L, FL_H)
        .extrude(FL_T)
        .edges("|X")
        .fillet(0.8)
        .faces(">X")
        .edges()
        .chamfer(0.3)
    )  # chamfer against elephant foot (lies on bed)
    b = head.union(fl)
    sel = cq.selectors.BoxSelector(
        (x_fl0 - 0.02, y - BTN_L / 2 - 0.1, BTN_Z - BTN_H / 2 - 0.1),
        (x_fl0 + 0.02, y + BTN_L / 2 + 0.1, BTN_Z + BTN_H / 2 + 0.1),
    )
    with contextlib.suppress(Exception):  # 0.4 groove between head and flange
        b = b.edges(sel).fillet(0.4)
    return b


def buttons_assembled():
    """Both button caps in board positions (single solid)."""
    return button(BTN_Y[0]).union(button(BTN_Y[1]))


def buttons_print():
    """Print bed: large flange face down, cap up -> no overhang, 2 pieces."""
    pieces = [
        button(0)
        .translate((0, 0, -BTN_Z))
        .rotate((0, 0, 0), (0, 1, 0), 90)
        .translate((0, i * 9.0, 0))
        for i in range(2)
    ]
    out = pieces[0].union(pieces[1])
    bb = out.val().BoundingBox()
    return out.translate((0, 0, -bb.zmin))


def lid():
    """Snap-on rear lid with lip, hooks, and hanging tab."""
    cap = outer_body().intersect(slab(Z_SPLIT, Z_BACK + 1))
    io = 0.15 + LIP_T
    hollow = (
        rrect(IN_X1 - IN_X0 - 2 * io, IN_Y1 - IN_Y0 - 2 * io, R_IN - io, Z_SPLIT - 1, Z_LID_IN)
        .faces(">Z")
        .edges()
        .fillet(1.0)
    )
    lid_part = cap.cut(hollow)
    # Lip that engages front shell
    lip = (
        cavity(0.15, Z_SPLIT - LIP_H, Z_SPLIT + 0.01, False)
        .union(
            cavity(0.15 - ROOT_CH, Z_SPLIT - ROOT_CH, Z_SPLIT + 0.01, False)
            .faces("<Z")
            .chamfer(ROOT_CH - 0.001)
        )  # 45° root instead of 90° notch
        .cut(cavity(io, Z_SPLIT - LIP_H - 1, Z_SPLIT + 1, False))
    )
    # Snap hooks: slot lip both sides, extend arm downward, latch bump
    for side in (-1, 1):
        xo = (IN_X1 - 0.15) if side > 0 else (IN_X0 + 0.15)
        xm = xo - side * SNAP_T / 2
        for y in SNAP_Y:
            for dy in (-1, 1):
                lip = lip.cut(
                    cq.Workplane("XY")
                    .workplane(offset=Z_SPLIT - LIP_H - 1)
                    .center(xm, y + dy * (SNAP_W + SNAP_SLOT) / 2)
                    .rect(3, SNAP_SLOT)
                    .extrude(LIP_H + 1)
                )
            arm = (
                cq.Workplane("XY")
                .workplane(offset=SNAP_Z0)
                .center(xm, y)
                .rect(SNAP_T, SNAP_W)
                .extrude(Z_SPLIT - SNAP_Z0 + 0.01)
            )
            lip = lip.union(arm).union(snap_profile(side, y, SNAP_W))
    lid_part = lid_part.union(lip)
    # no push rods: PCB is screwed down
    # Bottom hanging tab: tongue extends lip, nose top 0.45 flat (retaining edge),
    # then 45° -> when printing (lid flipped) no horizontal overhang
    z0 = Z_SPLIT - LIP_H - TAB_DROP
    yl = IN_Y0 + 0.15
    lid_part = lid_part.union(
        cq.Workplane("XY")
        .workplane(offset=z0)
        .center(17.0, yl + LIP_T / 2)
        .rect(TAB_W, LIP_T)
        .extrude(LIP_H + TAB_DROP)
    )
    zb, zt = z0 + 0.2, z0 + 1.4  # nose 1.2 mm tall
    nose = [
        (yl + 0.6, zb),
        (IN_Y0 - 0.55, zb),
        (IN_Y0 - 0.55, zt - 0.25),
        (IN_Y0 - 0.3, zt),
        (yl + 0.6, zt),
    ]
    return lid_part.union(
        cq.Workplane("YZ").workplane(offset=17.0 - TAB_W / 2).polyline(nose).close().extrude(TAB_W)
    )
    # no battery / no frame


# ------------------------------------------------------------------ Charging dock
TILT = 12.0  # tilt backward (deg)
POCKET = 9.0  # how deep device sits in dock
DOCK_CLR = 0.35
PLUG_W, PLUG_T = 12.4, 6.8  # 90° USB-C plug overmold (measure to confirm!)
PLUG_H = 14.0  # overmold height including cable bend
CABLE_D = 4.6
DOCK_W = 59.0


def to_dock(shape, base_z):
    """Device coordinates -> dock: USB bottom center = origin, upright, tilted."""
    return (
        shape.translate((-17.0, -OUT_Y0, -USB_Z))
        .rotate((0, 0, 0), (1, 0, 0), 90)
        .rotate((0, 0, 0), (0, 0, 1), 180)
        .rotate((0, 0, 0), (1, 0, 0), -TILT)
        .translate((0, 0, base_z))
    )


def dock():
    """Charging dock body (tilted pocket + cable channel)."""
    base_z = PLUG_H + 3.0  # support height under USB port
    top_z = base_z + POCKET
    c, sn = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))

    def tilt(y, z):  # point (Y,Z) in device side section -> dock
        return (y * c + z * sn, -y * sn + z * c + base_z)

    lip_wall = 4.0  # wall thickness in front/behind device
    # Front: parallel to display front, lip_wall in front
    fy0, fz0 = tilt(Z_FRONT - DOCK_CLR - lip_wall, 0.0)

    def front_y(z):
        return fy0 + (z - fz0) * sn / c

    by0, bz0 = tilt(Z_BACK + DOCK_CLR + lip_wall, 0.0)

    def back_y(z):
        return by0 + (z - bz0) * sn / c

    prof = [
        (front_y(0), 0),
        (back_y(0) + 6, 0),
        (back_y(top_z) + 6, top_z + 6),
        (back_y(top_z + 6), top_z + 6),
        (back_y(top_z), top_z),
        (front_y(top_z), top_z),
    ]
    prof = [
        (front_y(0), 0),
        (back_y(0) + 2, 0),
        (back_y(top_z + 7) + 2, top_z + 7),
        (front_y(top_z), top_z),
    ]
    body = cq.Workplane("YZ").workplane(offset=-DOCK_W / 2).polyline(prof).close().extrude(DOCK_W)
    body = body.edges("|X").fillet(3.0)
    body = body.faces("<X or >X").edges().fillet(2.5)
    pocket = to_dock(outer_body(DOCK_CLR), base_z)
    body = body.cut(pocket)
    # Clearance for button caps in pocket wall
    btn_gap = (
        cq.Workplane("XY")
        .box(BTN_PROUD + 1.5, 21.0, 9.0)
        .translate((OUT_X0 - (BTN_PROUD + 1.5) / 2 + 1.0, 10.75, BTN_Z))
    )
    body = body.cut(to_dock(btn_gap, base_z))
    # Plug shaft (open bottom), tilted like device
    shaft = (
        cq.Workplane("XY")
        .box(PLUG_W + 0.3, PLUG_T + 0.3, 60)
        .edges("|Z")
        .fillet(1.5)
        .translate((0, 0, -30 + 0.8))
    )
    shaft = shaft.rotate((0, 0, 0), (1, 0, 0), -TILT).translate((0, 0, base_z))
    body = body.cut(shaft)
    # Cable channel to rear (open bottom)
    body = body.cut(
        cq.Workplane("XY")
        .box(CABLE_D, 80, CABLE_D + 2)
        .translate((0, 36, (CABLE_D + 2) / 2 - 0.01))
    )
    # Clamp screw M3 from rear (pilot dia 2.6, M3x16 self-tapping)
    body = body.cut(
        cq.Workplane("XZ").workplane(offset=-60).center(0, base_z - 7.0).circle(1.3).extrude(60)
    )
    body = body.cut(
        cq.Workplane("XZ")
        .workplane(offset=-60)
        .center(0, base_z - 7.0)
        .circle(3.0)
        .extrude(60 - back_y(base_z - 7) - 2 + 2.0)
    )
    # Rubber feet dia 10
    y_a, y_b = front_y(0) + 8, back_y(0) - 6
    for x in (-DOCK_W / 2 + 8, DOCK_W / 2 - 8):
        for y in (y_a, y_b):
            body = body.cut(cq.Workplane("XY").center(x, y).circle(5.2).extrude(1.0))
    return body, base_z


def clean_stl(path):
    """Remove degenerate (zero-area) triangles from STL tessellation -> watertight."""
    m = trimesh.load(path, process=False)
    m = trimesh.Trimesh(m.vertices, m.faces, process=False)
    m.merge_vertices(digits_vertex=4)
    m.update_faces(m.nondegenerate_faces(height=1e-6))
    m.remove_unreferenced_vertices()
    trimesh.repair.fix_normals(m)
    m.export(path)
    return m.is_watertight


def drop_to_bed(workplane):
    """Center in XY and place bottom face on Z=0 (print bed)."""
    bb = workplane.val().BoundingBox()
    return workplane.translate((-bb.center.x, -bb.center.y, -bb.zmin))


if __name__ == "__main__":
    os.makedirs("out", exist_ok=True)
    parts = [
        ("gehaeuse_front", drop_to_bed(front_shell())),
        ("gehaeuse_deckel", drop_to_bed(lid().rotate((0, 0, 0), (1, 0, 0), 180))),
        ("tasten_2x", drop_to_bed(buttons_print())),
    ]
    for name, part in parts:
        path = f"out/{name}.stl"
        cq.exporters.export(part, path, tolerance=0.01, angularTolerance=0.1)
        clean_stl(path)
    print(f"Case outer: {W:.1f} x {H:.1f} x {T:.1f} mm")
