"""All-printed take on evb-sandwich for the W55RP20-EVB-Pico: same two blocks and the
opening between them, but the plate is printed in one piece with the frame-top, with the
project logo sunk into it, and PLA-friendly snaps replace the screws and inserts.

The snaps are short stiff arms in the bottom's side walls, 9 mm from root to tip, bending
sideways. Layers run along the arm, and strain at the root is about 1.1%: fine in PLA for
the few openings a case that stays shut gets.
Each arm stands on the bed, free of the floor, so nothing overhangs. A 45 deg barb at the
tip hooks 0.5 mm into a matching notch in the top.

The top prints on its long side: the opening then runs straight up, so the printed plate
and the deck are plain walls with no supports. The jack pocket's upper side wall is the one
long bridge left, 22 mm, and it comes out rough. The .stl files for the top are already
rotated for that; the .step files stay in case coordinates.

Run:  python evb_snapframe.py [path/to/W55RP20-EVB-Pico_V100.step]
With the board STEP given it checks overlaps and writes the coloured assembly too.
"""
import math
import os
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "evb-fdm"))
sys.path.insert(0, os.path.join(HERE, "..", "evb-sandwich"))
import evb_fdm as F  # noqa: E402
import evb_sandwich as S  # noqa: E402

S.X1 = 81.6                   # 1 mm longer than the sandwich at the USB end: the lip over the board's
                              # end is 1.5 mm instead of 0.5, and the USB-C mouth sits 1 mm inside the face
TOP_Z = S.UP_Z1 + F.FLOOR     # same overall height as the sandwich with its plate
SPAN_T = 3.0                  # printed plate over the opening
OPEN_Z1 = TOP_Z - SPAN_T

# snap arms: (root x, tip x); the tips point towards the middle
ARMS = [(6.0, 15.0), (69.0, 60.0)]
ARM_T, TAB_L = 1.2, 7.0
END_SLIT, BACK_GAP = 0.6, 0.6
BARB, WALL_GAP = 0.6, 0.1     # hook = BARB - WALL_GAP
END_GAP = 0.2                 # clip ends to the top's openings: the only thing locating the halves along x
SLOT_SHIFT = 0.0              # moves the top's openings along x, if a printed top sits off-centre
TAB_ROUND = 0.8               # clip tops rounded at both ends, to clear the openings' printed inside corners
TAB_TOP = 3.9

# logo: the icon's glyph, in its 24-unit grid, sunk into the top
LOGO_C = ((S.X0 + S.X1) / 2, F.BOARD_W / 2)
LOGO_S, LOGO_DEPTH = 2.0, 0.6
LOGO_LINES = [[(6.25, 8.5), (2.75, 12), (6.25, 15.5)], [(17.75, 8.5), (21.25, 12), (17.75, 15.5)]]
LOGO_STROKE = 1.75
LOGO_DOTS = [(8, 12), (12, 12), (16, 12)]
LOGO_DOT_R = 1.3

# BOOTSEL: a tongue in the deck with a nub over the button, pressed through the opening.
# BOOTSEL is the tact switch nearer the USB end (x 65.5); the other (x 59.5) is RUN.
BOOTSEL_X = 65.5
BTN_Y, BTN_TOP = 16.0, 2.5
TONGUE_A, TONGUE_W, TONGUE_T, SLIT = 8.0, 4.0, 1.2, 0.6   # root to nub, width, thickness
NUB_D, NUB_GAP = 2.4, 0.2
# board locating: the jack pocket's back wall sits 0.1 mm behind the RJ45 (back face x 17.05),
# so a drop or a cable pushed into the jack loads the jack against the top's front block, not
# the 0.5 mm lip under the USB-C port. Under the board's rear end the bottom is solid.
JACK_STOP = 17.15
# board pocket in the bottom, opened up for PETG: 0.35 mm a side and 0.3 mm at the USB end
# (the jack stop still keeps the board 0.1 mm off that end once the top is on)
BOARD_FIT = 0.1
BOARD_END = 80.3
# the wall stops at z 3.6 (the deck's underside), clear of the parts behind the jack
SHELF_X0 = 75.0
# thin skin over the LEDs: the deck only, from below; the plate stays full thickness
DECK_SKIN = 0.8


def ybox(x0, x1, ya, yb, z0, z1, sign):
    y0, y1 = (ya, yb) if sign > 0 else (F.BOARD_W - yb, F.BOARD_W - ya)
    return F.box(x0, x1, y0, y1, z0, z1)


def prism(pts, x0, x1, sign):
    p = cq.Workplane("YZ").polyline(pts).close().extrude(x1 - x0).translate((x0, 0, 0))
    return p if sign > 0 else p.mirror("XZ", (0, F.BOARD_W / 2, 0))


def snap(bottom, top, root, tip, sign):
    d = 1 if tip > root else -1
    lo, hi = min(root, tip), max(root, tip)
    t = S.Y0 + ARM_T
    z0 = S.LOW_Z0 - 1
    bottom = bottom.cut(ybox(lo, hi, t, t + BACK_GAP, z0, 1, sign))
    sx0, sx1 = (tip, tip + END_SLIT) if d > 0 else (tip - END_SLIT, tip)
    bottom = bottom.cut(ybox(sx0, sx1, S.Y0 - 1, t + BACK_GAP, z0, 1, sign))
    tx0, tx1 = (tip - TAB_L, tip) if d > 0 else (tip, tip + TAB_L)
    bottom = bottom.union(ybox(tx0, tx1, S.Y0, t, -0.01, TAB_TOP, sign).edges("|Y and >Z").fillet(TAB_ROUND))
    bottom = bottom.union(prism([(t - 0.01, 1.6), (t + BARB, 2.2), (t + BARB, 2.5), (t - 0.01, 3.7)], tx0, tx1, sign))
    top = top.cut(ybox(tx0 - END_GAP + SLOT_SHIFT, tx1 + END_GAP + SLOT_SHIFT, S.Y0 - 1, t + WALL_GAP, -0.01, TAB_TOP + 0.2, sign))
    top = top.cut(prism([(t, 1.55), (t + BARB + 0.1, 2.25), (t + BARB + 0.1, 2.65), (t, 3.85)],
                        tx0 - END_GAP + SLOT_SHIFT, tx1 + END_GAP + SLOT_SHIFT, sign))
    return bottom, top


def logo():
    def pt(p):
        return LOGO_C[0] + (p[0] - 12) * LOGO_S, LOGO_C[1] + (12 - p[1]) * LOGO_S

    z0, w = TOP_Z - LOGO_DEPTH, LOGO_STROKE * LOGO_S
    out = None
    for line in LOGO_LINES:
        for a, b in zip(line, line[1:]):
            (ax, ay), (bx, by) = pt(a), pt(b)
            s = (cq.Workplane("XY", origin=((ax + bx) / 2, (ay + by) / 2, z0))
                 .transformed(rotate=(0, 0, math.degrees(math.atan2(by - ay, bx - ax))))
                 .slot2D(math.hypot(bx - ax, by - ay) + w, w).extrude(LOGO_DEPTH + 1))
            out = s if out is None else out.union(s)
    for d in LOGO_DOTS:
        out = out.union(F.cyl(pt(d), 2 * LOGO_DOT_R * LOGO_S, z0, TOP_Z + 1))
    return out


def bootsel(top, bx):
    x0, x1 = bx - TONGUE_A, bx + NUB_D / 2 + 0.9
    y0, y1 = BTN_Y - TONGUE_W / 2, BTN_Y + TONGUE_W / 2
    top = top.cut(F.box(x0, x1 + SLIT, y0 - SLIT, y0, S.DECK_Z - 2.5, S.DECK_Z + 0.01))
    top = top.cut(F.box(x0, x1 + SLIT, y1, y1 + SLIT, S.DECK_Z - 2.5, S.DECK_Z + 0.01))
    top = top.cut(F.box(x1, x1 + SLIT, y0, y1, S.DECK_Z - 2.5, S.DECK_Z + 0.01))
    top = top.cut(F.box(x0, x1, y0, y1, F.TAIL_TOP + TONGUE_T, S.DECK_Z + 0.01))   # dip on top: a target
    return top.union(F.cyl((bx, BTN_Y), NUB_D, BTN_TOP + NUB_GAP, F.TAIL_TOP + 0.01))


def led_skins(top):
    for x0, x1, y0, y1 in F.WINDOWS:
        top = top.cut(F.box(x0, x1, y0, y1, F.TAIL_TOP - 0.01, S.DECK_Z - DECK_SKIN))
    return top


def build():
    # ---- top: blocks, the printed plate, the opening under it ----
    body = F.fillet(F.fillet(S.outline(S.LOW_Z0, TOP_Z), "<X or >X", 0.8), "<Z", 1.0)
    body = F.fillet(body, ">Z", 1.0)
    upper = F.box(S.X0 - 1, S.X1 + 1, S.Y0 - 1, S.Y1 + 1, 0, TOP_Z + 1)
    top, bottom = body.intersect(upper), body.cut(upper)      # split after filleting: no rounding at the joint
    top = top.cut(F.box(S.X0 + S.FACE_LIP, JACK_STOP, S.POCKET_Y0, S.POCKET_Y1, -0.01, S.UP_Z1))
    top = top.cut(F.box(S.X0 - 1, S.X0 + S.FACE_LIP + 0.01, F.JACK_Y0, F.JACK_Y1, -0.01, S.JACK_TOP + 0.25))
    top = top.cut(F.box(JACK_STOP - 0.01, S.REAR_IN, F.CAV_Y0, F.CAV_Y1, -0.01, F.TAIL_TOP))
    top = top.cut(F.box(S.FRONT_BLOCK_X1, S.REAR_BLOCK_X0, S.Y0 - 1, S.Y1 + 1, S.DECK_Z, OPEN_Z1))
    for x in (S.FRONT_BLOCK_X1, S.REAR_BLOCK_X0):
        top = F.fillet(top, cq.selectors.BoxSelector((x - 0.1, S.Y0 - 1, S.DECK_Z + 0.1), (x + 0.1, S.Y1 + 1, OPEN_Z1 - 0.1)),
                       S.EDGE_R)
    for y0, y1 in ((F.CAV_Y0, 0.6), (F.BOARD_W - 0.6, F.CAV_Y1)):
        top = top.union(F.box(20.0, F.CAV_X1, y0, y1, 0, F.TAIL_TOP))
    for xy in F.SCREWS:                                       # pads pressing the board down
        top = top.union(F.cyl(xy, F.BOSS_D, 0, F.TAIL_TOP + 0.01))
    top = top.cut(logo())

    # ---- bottom ----
    bottom = bottom.cut(F.box(F.CAV_X0, S.REAR_IN, F.CAV_Y0, F.CAV_Y1, F.UNDER_Z, 1))
    bottom = bottom.cut(F.box(F.CAV_X0, BOARD_END, F.CAV_Y0 - BOARD_FIT, F.CAV_Y1 + BOARD_FIT, -F.BOARD_T - 0.1, 1))
    bottom = bottom.cut(F.box(S.X0 - 1, F.CAV_X0 + 0.01, F.JACK_Y0, F.JACK_Y1, -0.2, 1))
    for y0, y1 in ((F.CAV_Y0, 0.6), (F.BOARD_W - 0.6, F.CAV_Y1)):
        bottom = bottom.union(F.box(F.CAV_X0, F.CAV_X1, y0, y1, F.UNDER_Z, -F.BOARD_T))
    for xy in F.SCREWS:
        bottom = bottom.union(F.cyl(xy, F.BOSS_D, F.UNDER_Z - 0.01, -F.BOARD_T))

    usb = (cq.Workplane("YZ", origin=(F.CAV_X1 - 2, F.USB_Y, F.USB_Z))
           .placeSketch(F.rrect(F.USB_W, F.USB_H, 1.0)).extrude(10))
    usb = usb.union(cq.Workplane("YZ", origin=(S.X1 - 0.5, F.USB_Y, F.USB_Z))
                    .placeSketch(F.rrect(F.USB_W, F.USB_H, 1.0),
                                 F.rrect(F.USB_W + 1.0, F.USB_H + 1.0, 1.5).moved(cq.Location(cq.Vector(0, 0, 0.5))))
                    .loft())
    # the receptacle is flush with the board's top face on the real board (WIZnet's STEP
    # shows it 0.99 mm lower), so only the top is cut
    top = top.cut(usb)

    bottom = bottom.union(F.box(SHELF_X0, S.REAR_IN + 0.01, F.CAV_Y0, F.CAV_Y1, F.UNDER_Z - 0.01, -F.BOARD_T))

    for root, tip in ARMS:
        for sign in (1, -1):
            bottom, top = snap(bottom, top, root, tip, sign)

    top = bootsel(top, BOOTSEL_X)
    top = led_skins(top)
    return {"top": top, "bottom": bottom}


def on_side(part):
    """Long side on the bed, for the print STL."""
    return part.rotate((0, 0, 0), (1, 0, 0), 90).translate((0, 0, -S.Y0))


if __name__ == "__main__":
    parts = build()
    for name, part in parts.items():
        base = os.path.join(HERE, f"evb-snapframe-{name}")
        cq.exporters.export(part, base + ".step")
        cq.exporters.export(part if name == "bottom" else on_side(part), base + ".stl",
                            tolerance=0.02, angularTolerance=0.1)
        bb = part.val().BoundingBox()
        print(f"{name}: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm, {part.val().Volume() / 1000:.1f} cm3,"
              f" valid {part.val().isValid()}, solids {len(part.solids().vals())}")
    b = on_side(parts["top"]).val().BoundingBox()
    print(f"top as printed: z {b.zmin:.2f}..{b.zmax:.2f}")
    print("top/bottom overlap", round(parts["top"].val().intersect(parts["bottom"].val()).Volume(), 3), "mm3")
    print(f"opening {S.REAR_BLOCK_X0 - S.FRONT_BLOCK_X1:.1f} x {OPEN_Z1 - S.DECK_Z:.2f} mm; overall height {TOP_Z - S.LOW_Z0:.1f}")
    if len(sys.argv) > 1:
        import assembly as A
        evb = A.evb_parts(sys.argv[1])
        bad = 0
        for _, _, s, _ in evb:
            for name, part in parts.items():
                v = part.val().intersect(s).Volume()
                if v > 1e-3:
                    c = s.BoundingBox().center
                    print(f"  overlap {v:.3f} mm3 at x {c.x:.1f} y {c.y:.1f} z {c.z:.1f} ({name})")
                    bad += 1
        print("board overlap check:", "clear" if not bad else f"{bad} hits")
        case = [("frame-bottom", "yellow", parts["bottom"].val(), 0.0), ("frame-top", "clear", parts["top"].val(), 0.0)]
        A.write_usdz(os.path.join(HERE, "evb-snapframe-assembly.usdz"), case + evb)
        A.write_usdz(os.path.join(HERE, "evb-snapframe-exploded.usdz"),
                     [(n, m, s, -8.0 if m == "yellow" else 8.0) for n, m, s, _ in case] + evb)
