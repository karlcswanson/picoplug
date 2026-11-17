"""FDM shells for the WIZnet W55RP20-EVB-Pico, tuned for a 0.4 mm nozzle.

Same layout as ../evb/evb_shell.py (the resin version): a tray wraps the board and
the base of the RJ45, the jack's metal shield stays exposed above a flat deck, two
halves split at the board's top face with a lap joint. Changes for FDM: thicker
walls, deck and tongue, wider slits, stiffer clips, and M2 heat-set inserts in the
top instead of pilot holes.

Variants:
  screw  2 M2 screws at the USB end (into heat-set inserts) + 2 snap clips by the jack
  snap   4 snap clips, no screws, no tools
Each has two tops:
  top          solid; print in translucent PETG so the LEDs glow through
  top-windows  slots over the status LEDs and PWR; for opaque filament

Frame of WIZnet's STEP (W55RP20-EVB-Pico_V100.step): x along the board (jack face
at x = -4.75, USB end at x = 80), y across (0..21), z up with the board's top face
at z = 0.

Run:  python evb_fdm.py [path/to/W55RP20-EVB-Pico_V100.step]
Writes evb-fdm-<variant>-bottom/top/top-windows .step and .stl next to this file.
With the board STEP given, it also reports any overlap with the board.
"""
import os
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- parameters ----
WALL = 1.6                    # 4 lines at 0.4
FLOOR = 1.6                   # floor and deck
BOARD_L, BOARD_W, BOARD_T = 80.0, 21.0, 1.62
SPLIT_Z = 0.0                 # parting plane: top face of the board

CAV_X0, CAV_X1 = -0.2, BOARD_L + 0.25
CAV_Y0, CAV_Y1 = -0.25, BOARD_W + 0.25
OUT_Y0, OUT_Y1 = CAV_Y0 - WALL, CAV_Y1 + WALL
JACK_FACE_X = -4.75
FRONT_X = JACK_FACE_X - 1.0   # shell front face, 1 mm proud of the jack face
REAR_X = 80.6 + 0.35          # USB-C mouth is at 80.6, 0.35 inside the rear face

UNDER_Z = -3.6                # under the board: through-hole pins reach -3.25
TAIL_TOP = 3.6                # tallest tail part is the USB-C receptacle, 3.31
DECK_Z = TAIL_TOP + FLOOR

# RJ45 body: x -4.75..17.05, y 2.55..18.45, 13.35 tall
JACK_X1 = 17.05 + 0.25
JACK_Y0, JACK_Y1 = 2.55 - 0.25, 18.45 + 0.25

# USB-C receptacle: y 6.03..14.97, z -0.99..3.31
USB_W = 8.94 + 0.5
USB_H = 4.30 + 0.5
USB_Y = 10.5
USB_Z = (-0.99 + 3.31) / 2

SCREWS = [(78.0, 3.5), (78.0, 17.5)]      # board holes, 2.0 mm
SCREW_CLEAR = 2.3
SCREW_HEAD = 4.2              # M2 socket or pan head
SCREW_HEAD_D = 1.4
INSERT_D = 3.2                # M2 x 3 heat-set insert (3.2 mm hole)
INSERT_DEPTH = 4.4            # leaves 0.8 mm of deck above
BOSS_D = 4.8                  # clears the USB receptacle (y 6.03) by 0.13
TONGUE = 1.0
TONGUE_W = 0.8
FIT = 0.15

# snap clips: a tab in the side wall of the bottom half, cut free by two slits and
# rooted in the floor, rising above the split; a barb on its inside catches a notch
# in the top
CLIP_W = 6.0
CLIP_T = 1.2
CLIP_TOP = 2.8
SLIT = 0.6
BARB = 0.4                    # barb depth = tab deflection when snapping
BARB_Z0, BARB_Z1 = 1.6, 2.6
CLIPS = {"screw": [4.0], "snap": [4.0, 66.0]}     # clip start x, both sides

# light windows over the LEDs (top-windows only)
WINDOWS = [(48.7, 51.3, 8.0, 15.4),     # USER, ACT, DUP, SPD, LINK column
           (60.2, 62.8, 4.6, 6.4)]      # PWR


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cyl(xy, d, z0, z1):
    return cq.Workplane("XY").center(*xy).circle(d / 2).extrude(z1 - z0).translate((0, 0, z0))


def fillet(w, sel, r):
    for rr in (r, r * 0.7, r * 0.5):
        try:
            out = w.edges(sel).fillet(rr)
            if out.val().isValid():
                return out
        except Exception:
            pass
    return w


def rrect(w, h, r):
    return cq.Sketch().rect(w, h).vertices().fillet(r)


BOT_Z = UNDER_Z - FLOOR


def wall_ring(inset_out, inset_in, z0, h):
    """Ring following the outer outline in plan, between two insets from it."""
    cx, lx = (REAR_X + FRONT_X) / 2, REAR_X - FRONT_X
    cy, ly = (OUT_Y0 + OUT_Y1) / 2, OUT_Y1 - OUT_Y0
    outline = cq.Workplane("XY").rect(lx, ly).extrude(h).translate((cx, cy, z0)).edges("|Z").fillet(3.0)
    o = cq.Workplane("XY").rect(lx - 2 * inset_out, ly - 2 * inset_out).extrude(h).translate((cx, cy, z0))
    i = cq.Workplane("XY").rect(lx - 2 * inset_in, ly - 2 * inset_in).extrude(h).translate((cx, cy, z0))
    return outline.intersect(o).cut(i)


def clip(part_bottom, part_top, x0, sign):
    """Clip from x0 on the y = 0 side (sign 1) or the y = BOARD_W side (sign -1, mirrored)."""
    x1 = x0 + CLIP_W

    def bx(xa, xb, ya, yb, z0, z1):
        y0, y1 = (ya, yb) if sign > 0 else (BOARD_W - yb, BOARD_W - ya)
        return box(xa, xb, y0, y1, z0, z1)

    tab_in = OUT_Y0 + CLIP_T
    # bottom: free the tab down into the floor, then build tab and barb
    part_bottom = part_bottom.cut(bx(x0 - SLIT, x1 + SLIT, OUT_Y0 - 1, CAV_Y0, BOT_Z + 0.8, 10))
    part_bottom = part_bottom.union(bx(x0, x1, OUT_Y0, tab_in, BOT_Z + 0.79, CLIP_TOP))
    barb = (cq.Workplane("YZ").polyline([(tab_in - 0.01, BARB_Z0), (tab_in + BARB, BARB_Z0),
                                         (tab_in + BARB, BARB_Z0 + 0.3), (tab_in - 0.01, BARB_Z1)])
            .close().extrude(CLIP_W).translate((x0, 0, 0)))
    if sign < 0:
        barb = barb.mirror("XZ", (0, BOARD_W / 2, 0))
    part_bottom = part_bottom.union(barb)
    # top: pocket for the tab, notch for the barb
    part_top = part_top.cut(bx(x0 - SLIT, x1 + SLIT, OUT_Y0 - 1, tab_in + 0.15, SPLIT_Z, CLIP_TOP + 0.2))
    part_top = part_top.cut(bx(x0 - 0.15, x1 + 0.15, tab_in, tab_in + BARB + 0.2, BARB_Z0 - 0.15, BARB_Z1 + 0.15))
    return part_bottom, part_top


def build(variant, deck_z=DECK_Z, jack_top=40):
    """jack_top below deck_z covers the jack (used by ../evb-tall)."""
    body = box(FRONT_X, REAR_X, OUT_Y0, OUT_Y1, BOT_Z, deck_z)
    body = fillet(body, "|Z", 3.0)
    body = fillet(body, ">Z or <X or >X", 1.0)

    # cavity: under the board, the board itself, the tail above it, and the jack opening
    cavity = box(CAV_X0, CAV_X1, CAV_Y0, CAV_Y1, UNDER_Z, SPLIT_Z)
    cavity = cavity.union(box(JACK_X1 - 0.01, CAV_X1, CAV_Y0, CAV_Y1, SPLIT_Z, TAIL_TOP))
    opening = box(FRONT_X - 1, JACK_X1, JACK_Y0, JACK_Y1, SPLIT_Z, jack_top)
    # lip under the jack face hides the board edge; 0.2 mm under the overhanging jack
    opening = opening.union(box(FRONT_X - 1, CAV_X0 + 0.01, JACK_Y0, JACK_Y1, SPLIT_Z - 0.2, SPLIT_Z))
    cavity = cavity.union(opening)
    shell = body.cut(cavity)

    # board ledges along both long edges (bottom), ribs pressing the edges in the tail (top)
    for y0, y1 in ((CAV_Y0, 0.6), (BOARD_W - 0.6, CAV_Y1)):
        shell = shell.union(box(CAV_X0, CAV_X1, y0, y1, UNDER_Z, -BOARD_T))
        shell = shell.union(box(20.0, CAV_X1, y0, y1, SPLIT_Z, TAIL_TOP))     # starts past the 0402 at x 19, y 20.3

    # bosses under the board (both variants) and over it (screw variant)
    for xy in SCREWS:
        shell = shell.union(cyl(xy, BOSS_D, UNDER_Z, -BOARD_T))
        if variant == "screw":
            shell = shell.union(cyl(xy, BOSS_D, SPLIT_Z, TAIL_TOP))

    above = box(-100, 200, -100, 100, SPLIT_Z, 100)
    top = shell.intersect(above)
    bottom = shell.cut(above)

    tongue = wall_ring(WALL - TONGUE_W, WALL, SPLIT_Z, TONGUE).cut(cavity)
    groove = wall_ring(WALL - TONGUE_W - FIT, WALL + FIT, SPLIT_Z, TONGUE + FIT)
    bottom = bottom.union(tongue)
    top = top.cut(groove)

    if variant == "screw":
        for xy in SCREWS:
            bottom = bottom.cut(cyl(xy, SCREW_CLEAR, -20, 20))
            bottom = bottom.cut(cyl(xy, SCREW_HEAD, BOT_Z - 1, BOT_Z + SCREW_HEAD_D))
            top = top.cut(cyl(xy, INSERT_D, SPLIT_Z - 1, SPLIT_Z + INSERT_DEPTH))

    usb = (cq.Workplane("YZ", origin=(0, USB_Y, USB_Z)).placeSketch(rrect(USB_W, USB_H, 1.0)).extrude(10)
           .translate((CAV_X1 - 2, 0, 0)))
    usb = usb.union(cq.Workplane("YZ", origin=(REAR_X - 0.5, USB_Y, USB_Z))
                    .placeSketch(rrect(USB_W, USB_H, 1.0),
                                 rrect(USB_W + 1.0, USB_H + 1.0, 1.5).moved(cq.Location(cq.Vector(0, 0, 0.5))))
                    .loft())
    top = top.cut(usb)
    bottom = bottom.cut(usb)

    for x0 in CLIPS[variant]:
        for sgn in (1, -1):
            bottom, top = clip(bottom, top, x0, sgn)

    top_windows = top
    for x0, x1, y0, y1 in WINDOWS:
        win = (cq.Workplane("XY", origin=((x0 + x1) / 2, (y0 + y1) / 2, TAIL_TOP - 0.5))
               .placeSketch(rrect(x1 - x0, y1 - y0, 0.6)).extrude(FLOOR + 1))
        top_windows = top_windows.cut(win)
    return {"bottom": bottom, "top": top, "top-windows": top_windows}


if __name__ == "__main__":
    board = []
    if len(sys.argv) > 1:
        for s in cq.importers.importStep(sys.argv[1]).solids().vals():
            b = s.BoundingBox()
            if b.zmax > 8 and b.xmin > 18:
                continue            # parts in WIZnet's model that aren't on the board
            board.append(s)

    for variant in ("screw", "snap"):
        parts = build(variant)
        for name, part in parts.items():
            base = os.path.join(HERE, f"evb-fdm-{variant}-{name}")
            cq.exporters.export(part, base + ".step")
            cq.exporters.export(part, base + ".stl", tolerance=0.02, angularTolerance=0.1)
            bb = part.val().BoundingBox()
            print(f"{variant} {name}: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm,"
                  f" volume {part.val().Volume() / 1000:.2f} cm3, valid {part.val().isValid()},"
                  f" solids {len(part.solids().vals())}")
        if board:
            worst = 0.0
            for s in board:
                for name in ("top", "bottom"):
                    v = parts[name].val().intersect(s).Volume()
                    if v > 1e-3:
                        b = s.BoundingBox()
                        print(f"  overlap {v:.3f} mm3 with part at x {b.center.x:.1f} y {b.center.y:.1f}"
                              f" z {b.center.z:.1f} ({name})")
                        worst = max(worst, v)
            v = parts["top"].val().intersect(parts["bottom"].val()).Volume()
            print(f"  {variant} overlap check:", "clear" if worst == 0 else f"worst {worst:.3f} mm3",
                  f"| top/bottom intersection {v:.3f} mm3")
