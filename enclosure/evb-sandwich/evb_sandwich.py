"""Framed sandwich for the W55RP20-EVB-Pico: a printed frame closes the sides and bottom,
with an aluminum plate on top.

The RJ45 cage's spring tabs stick out from its top and sides. The frame keeps them all
inside: the cage sits in a pocket TAB_CLR bigger than its body on the top and both
sides, under the full-length top plate, behind a FACE_LIP thick lip that fits close
round the cage at the front face. The jack face and the USB-C mouth are flush with the
ends; only they are open.

As on ../evb-tall, the frame-top rises into two blocks, one round the cage and one over
the USB-C, and is open under the top plate between them down to a deck over the board:
a cable, strap or clip can loop round the plate.

Parts:
  plate             1.6 mm aluminum, laser cut, rounded corners, countersunk M2 holes,
                    otherwise solid
  frame-top         printed in clear PLA or PETG, so the LEDs glow through the deck:
                    side walls, front lip, the pocket round the cage, the deck, ribs
                    pressing the board edges
  frame-top-windows the same with slots in the deck, for opaque filament
  frame-bottom      printed, yellow: the outside bottom, tray under the board with ledges
                    and bosses, and the block in front of the board under the jack
Fasteners, all M2 flat head (90 deg) into heat-set inserts in the frame-top:
  plate          4 x M2 x 5
  frame-bottom   4 x M2 x 8, countersunk into its underside: 2 beside the jack, 2
                 through the board's holes. They join the two halves and clamp the board.

Writes evb-sandwich-frame-top / frame-top-windows / frame-bottom .step/.stl,
evb-sandwich-plate-top .step and .dxf (flat outline for laser cutting).

Run:  python evb_sandwich.py [path/to/W55RP20-EVB-Pico_V100.step]
"""
import os
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "evb-fdm"))
import evb_fdm as F  # noqa: E402

TAB_CLR = 1.2                 # round the cage body on top and both sides; measure the tabs
JACK_Y0, JACK_Y1 = 2.55, 18.45
JACK_TOP = 13.35
POCKET_Y0, POCKET_Y1 = JACK_Y0 - TAB_CLR, JACK_Y1 + TAB_CLR
POCKET_X1 = 17.05 + TAB_CLR
FACE_LIP = 1.0                # front lip round the cage, 0.25 clear; the pocket is behind it

X0, X1 = F.JACK_FACE_X, 80.6                  # flush with the jack face and the USB-C mouth
REAR_IN = X1 - 1.2                            # inside of the rear wall, off the board
BOARD_END = F.BOARD_L + 0.1                   # the board's slot runs into the rear wall
Y0, Y1 = -3.5, F.BOARD_W + 3.5                # 28 wide: room for inserts beside the cage
R = 4.0                                       # plan corner radius
LOW_Z0 = F.UNDER_Z - F.FLOOR                  # frame bottom
UP_Z1 = JACK_TOP + TAB_CLR                    # frame top, under the top plate
PLATE_T = 1.6
DECK_Z = F.TAIL_TOP + F.FLOOR                 # deck over the board, under the opening
FRONT_BLOCK_X1 = 22.3                         # block round the cage ends 4 mm behind the pocket
REAR_BLOCK_X0 = 70.0                          # block over the USB-C (x 72.7..80.6)
EDGE_R = 1.0

INSERT_D, INSERT_DEPTH = 3.2, 4.4             # M2 x 3 heat-set inserts
HOLE_D, CSK_D = 2.4, 4.4
TOP_SCREWS = [(8.0, (Y0 + POCKET_Y0) / 2), (8.0, (Y1 + POCKET_Y1) / 2)] + F.SCREWS
FRONT_SCREWS = [(-1.5, (Y0 + POCKET_Y0) / 2), (-1.5, (Y1 + POCKET_Y1) / 2)]   # beside the jack
REAR_SCREWS = F.SCREWS                        # through the board's holes into the bosses


def box(x0, x1, y0, y1, z0, z1):
    return F.box(x0, x1, y0, y1, z0, z1)


def outline(z0, z1):
    return (cq.Workplane("XY", origin=((X0 + X1) / 2, (Y0 + Y1) / 2, z0))
            .placeSketch(F.rrect(X1 - X0, Y1 - Y0, R)).extrude(z1 - z0))


def csk(xy, z, up):
    """Countersink cone for an M2 flat head whose head is at z, screw pointing up or down."""
    d = 1 if up else -1
    return cq.Workplane("XY").add(cq.Solid.makeCone(CSK_D / 2 + 0.01, 0, CSK_D / 2 + 0.01,
                                                   cq.Vector(xy[0], xy[1], z - 0.01 * d), cq.Vector(0, 0, d)))


def frame():
    body = F.fillet(F.fillet(outline(LOW_Z0, UP_Z1), "<X or >X", 0.8), "<Z", 1.0)
    upper = box(X0 - 1, X1 + 1, Y0 - 1, Y1 + 1, 0, UP_Z1 + 1)
    top = body.intersect(upper)
    bottom = body.cut(upper)

    # ---- frame-top: split face flat at the board's top, prints split face down ----
    # pocket round the cage, the lip at the face
    top = top.cut(box(X0 + FACE_LIP, POCKET_X1, POCKET_Y0, POCKET_Y1, -0.01, UP_Z1 + 1))
    top = top.cut(box(X0 - 1, X0 + FACE_LIP + 0.01, F.JACK_Y0, F.JACK_Y1, -0.01, JACK_TOP + 0.25))
    # over the tail: room for the parts under the deck, the opening above it
    top = top.cut(box(POCKET_X1 - 0.01, REAR_IN, F.CAV_Y0, F.CAV_Y1, -0.01, F.TAIL_TOP))
    top = top.cut(box(FRONT_BLOCK_X1, REAR_BLOCK_X0, Y0 - 1, Y1 + 1, DECK_Z, UP_Z1 + 1))
    for x in (FRONT_BLOCK_X1, REAR_BLOCK_X0):
        top = F.fillet(top, cq.selectors.BoxSelector((x - 0.1, Y0 - 1, DECK_Z + 0.1), (x + 0.1, Y1 + 1, UP_Z1 - 0.1)),
                       EDGE_R)
    for y0, y1 in ((F.CAV_Y0, 0.6), (F.BOARD_W - 0.6, F.CAV_Y1)):
        top = top.union(box(20.0, F.CAV_X1, y0, y1, 0, F.TAIL_TOP))
    for xy in REAR_SCREWS:
        top = top.union(F.cyl(xy, F.BOSS_D, 0, F.TAIL_TOP + 0.01))
    for xy in REAR_SCREWS + FRONT_SCREWS:
        top = top.cut(F.cyl(xy, INSERT_D, -1, INSERT_DEPTH))
    for xy in TOP_SCREWS:
        top = top.cut(F.cyl(xy, INSERT_D, UP_Z1 - INSERT_DEPTH, UP_Z1 + 1))

    # ---- frame-bottom: tray, and the block in front of the board under the jack ----
    bottom = bottom.cut(box(F.CAV_X0, REAR_IN, F.CAV_Y0, F.CAV_Y1, F.UNDER_Z, 1))
    bottom = bottom.cut(box(F.CAV_X0, BOARD_END, F.CAV_Y0, F.CAV_Y1, -F.BOARD_T - 0.1, 1))
    bottom = bottom.cut(box(X0 - 1, F.CAV_X0 + 0.01, F.JACK_Y0, F.JACK_Y1, -0.2, 1))     # lip under the jack face
    for y0, y1 in ((F.CAV_Y0, 0.6), (F.BOARD_W - 0.6, F.CAV_Y1)):
        bottom = bottom.union(box(F.CAV_X0, F.CAV_X1, y0, y1, F.UNDER_Z, -F.BOARD_T))
    for xy in REAR_SCREWS:
        bottom = bottom.union(F.cyl(xy, F.BOSS_D, F.UNDER_Z - 0.01, -F.BOARD_T))
    for xy in REAR_SCREWS + FRONT_SCREWS:
        bottom = bottom.cut(F.cyl(xy, F.SCREW_CLEAR, LOW_Z0 - 1, 1))
        bottom = bottom.cut(csk(xy, LOW_Z0, True))

    usb = (cq.Workplane("YZ", origin=(F.CAV_X1 - 2, F.USB_Y, F.USB_Z))
           .placeSketch(F.rrect(F.USB_W, F.USB_H, 1.0)).extrude(10))
    usb = usb.union(cq.Workplane("YZ", origin=(X1 - 0.5, F.USB_Y, F.USB_Z))
                    .placeSketch(F.rrect(F.USB_W, F.USB_H, 1.0),
                                 F.rrect(F.USB_W + 1.0, F.USB_H + 1.0, 1.5).moved(cq.Location(cq.Vector(0, 0, 0.5))))
                    .loft())
    top, bottom = top.cut(usb), bottom.cut(usb)
    top_windows = top
    for x0, x1, y0, y1 in F.WINDOWS:
        top_windows = top_windows.cut(cq.Workplane("XY", origin=((x0 + x1) / 2, (y0 + y1) / 2, F.TAIL_TOP - 0.5))
                                      .placeSketch(F.rrect(x1 - x0, y1 - y0, 0.6)).extrude(F.FLOOR + 1))
    return top, top_windows, bottom


def plate_flat(holes, windows=()):
    p = cq.Workplane("XY", origin=((X0 + X1) / 2, (Y0 + Y1) / 2, 0)).placeSketch(
        F.rrect(X1 - X0, Y1 - Y0, R)).extrude(PLATE_T)
    for xy in holes:
        p = p.cut(F.cyl(xy, HOLE_D, -1, PLATE_T + 1))
    for x0, x1, y0, y1 in windows:
        p = p.cut(cq.Workplane("XY", origin=((x0 + x1) / 2, (y0 + y1) / 2, -1))
                  .placeSketch(F.rrect(x1 - x0, y1 - y0, 0.6)).extrude(PLATE_T + 2))
    return p


def plate():
    flat = plate_flat(TOP_SCREWS)                # solid: mounting holes only
    top = flat.translate((0, 0, UP_Z1))
    for xy in TOP_SCREWS:
        top = top.cut(csk(xy, UP_Z1 + PLATE_T, False))
    return top, flat


def build():
    ft, ftw, fb = frame()
    pt, pt_flat = plate()
    return {"frame-top": ft, "frame-top-windows": ftw, "frame-bottom": fb, "plate-top": pt,
            "plate-top-flat": pt_flat}


if __name__ == "__main__":
    out = build()
    for name in ("frame-top", "frame-top-windows", "frame-bottom"):
        part = out[name]
        base = os.path.join(HERE, f"evb-sandwich-{name}")
        cq.exporters.export(part, base + ".step")
        cq.exporters.export(part, base + ".stl", tolerance=0.02, angularTolerance=0.1)
        bb = part.val().BoundingBox()
        print(f"{name}: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm, valid {part.val().isValid()},"
              f" solids {len(part.solids().vals())}")
    cq.exporters.export(out["plate-top"], os.path.join(HERE, "evb-sandwich-plate-top.step"))
    cq.exporters.export(out["plate-top-flat"].faces("<Z"), os.path.join(HERE, "evb-sandwich-plate-top.dxf"))
    print(f"plate: {X1 - X0:.1f} x {Y1 - Y0:.1f} x {PLATE_T} mm")
    print(f"assembly {X1 - X0:.1f} x {Y1 - Y0:.1f} x {UP_Z1 + PLATE_T - LOW_Z0:.1f} mm;"
          f" cage pocket {TAB_CLR} mm over the top and sides;"
          f" opening under the top plate {REAR_BLOCK_X0 - FRONT_BLOCK_X1:.1f} long x {UP_Z1 - DECK_Z:.1f} tall")

    names = ["frame-top", "frame-bottom", "plate-top"]
    ok = True
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            v = out[a].val().intersect(out[b].val()).Volume()
            if v > 1e-3:
                print(f"  overlap {a}/{b} {v:.3f} mm3"); ok = False
    if len(sys.argv) > 1:
        for s in cq.importers.importStep(sys.argv[1]).solids().vals():
            bb = s.BoundingBox()
            if (bb.zmax > 8 and bb.xmin > 18) or (bb.zmin < -3 and bb.xlen < 1):
                continue            # parts in WIZnet's model that aren't on the board
            for name in names:
                v = out[name].val().intersect(s).Volume()
                if v > 1e-3:
                    print(f"  overlap {v:.3f} mm3 with part at x {bb.center.x:.1f} y {bb.center.y:.1f}"
                          f" z {bb.center.z:.1f} ({name})"); ok = False
    print("overlap check:", "clear" if ok else "see above")
