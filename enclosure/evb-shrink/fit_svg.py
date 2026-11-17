"""Draw how the W55RP20-EVB-Pico sits in 1-1/2" (38.1 mm) 2:1 heat shrink before shrinking.
Dimensions are from ../evb-fdm/evb_fdm.py (measured on WIZnet's STEP). z 0 is the board's top face."""
import math

TUBES = [("1-1/2\"", 38.1, 19.05, "#4a7fb5"), ("1\"", 25.4, 12.7, "#d35400")]   # Alpha FIT-221 2:1: supplied, recovered ID
BOARD_W, BOARD_T = 21.0, 1.62
JACK_Y, JACK_TOP, PIN_Z = (2.55, 18.45), 13.35, -3.25
USB_Y, USB_TOP = (6.03, 14.97), 3.31
LENGTH = 80.6 + 4.75                          # jack face to USB-C mouth

jack = [(0, -BOARD_T), (JACK_Y[0], PIN_Z), (JACK_Y[1], PIN_Z), (BOARD_W, -BOARD_T), (BOARD_W, 0),
        (JACK_Y[1], 0), (JACK_Y[1], JACK_TOP), (JACK_Y[0], JACK_TOP), (JACK_Y[0], 0), (0, 0)]
tail = [(0, -BOARD_T), (BOARD_W, -BOARD_T), (BOARD_W, 0), (USB_Y[1], 0), (USB_Y[1], USB_TOP),
        (USB_Y[0], USB_TOP), (USB_Y[0], 0), (0, 0)]


def hull(pts):
    pts = sorted(set(pts))
    def half(p):
        h = []
        for q in p:
            while len(h) > 1 and (h[-1][0]-h[-2][0])*(q[1]-h[-2][1]) - (h[-1][1]-h[-2][1])*(q[0]-h[-2][0]) <= 0:
                h.pop()
            h.append(q)
        return h
    return half(pts)[:-1] + half(pts[::-1])[:-1]


def perim(h):
    return sum(math.dist(h[i], h[i-1]) for i in range(len(h)))


def bbox(p):
    xs, ys = zip(*p)
    return min(xs), max(xs), min(ys), max(ys)


S = 6.0          # px per mm
W, H = 1180, 860
out = []
def t(x, y, s, size=15, anchor="start", weight="normal", fill="#222"):
    out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" text-anchor="{anchor}" '
               f'font-weight="{weight}" fill="{fill}">{s}</text>')


def section(cx, cy, shape, title, mm_h):
    """Cross-section centred in the tube circle; cx, cy in px."""
    hp = hull(shape)
    x0, x1, z0, z1 = bbox(shape)
    ox, oz = (x0 + x1) / 2, (z0 + z1) / 2
    P = lambda x, z: (cx + (x - ox) * S, cy - (z - oz) * S)
    for i, (_, d, _, col) in enumerate(TUBES):
        out.append(f'<circle cx="{cx}" cy="{cy}" r="{d / 2 * S:.1f}" fill="{"#eef5fb" if i == 0 else "none"}" stroke="{col}" stroke-width="2.2"/>')
    r = TUBES[0][1] / 2 * S
    out.append('<polygon points="' + " ".join("%.1f,%.1f" % P(*q) for q in hp) +
               '" fill="none" stroke="#c0392b" stroke-width="1.4" stroke-dasharray="3 3"/>')
    # board, jack/usb
    bx0, by0 = P(0, 0); bx1, by1 = P(BOARD_W, -BOARD_T)
    out.append(f'<rect x="{bx0:.1f}" y="{by0:.1f}" width="{bx1-bx0:.1f}" height="{by1-by0:.1f}" fill="#2e6b3a"/>')
    for (ya, yb), za, zb, col in mm_h:
        ax, az = P(ya, zb); bx, bz = P(yb, za)
        out.append(f'<rect x="{ax:.1f}" y="{az:.1f}" width="{bx-ax:.1f}" height="{bz-az:.1f}" fill="{col}" stroke="#555"/>')
    # dims
    lx0, _ = P(0, 0); lx1, _ = P(BOARD_W, 0); _, ly = P(0, z0)
    ly += 34
    out.append(f'<line x1="{lx0:.1f}" y1="{ly}" x2="{lx1:.1f}" y2="{ly}" stroke="#222" marker-start="url(#a)" marker-end="url(#a)"/>')
    t((lx0 + lx1) / 2, ly + 18, f"{x1 - x0:.1f}", 13, "middle")
    _, hz0 = P(0, z0); _, hz1 = P(0, z1); hx = lx1 + 34
    out.append(f'<line x1="{hx:.1f}" y1="{hz0:.1f}" x2="{hx:.1f}" y2="{hz1:.1f}" stroke="#222" marker-start="url(#a)" marker-end="url(#a)"/>')
    t(hx + 6, (hz0 + hz1) / 2 + 5, f"{z1 - z0:.1f}", 13)
    p = perim(hp)
    t(cx, cy - r - 52, title, 18, "middle", "bold")
    t(cx, cy - r - 30, f"wrap needed (dashed red): {p:.0f} mm around", 14, "middle", fill="#c0392b")
    for i, (name, d, dr, col) in enumerate(TUBES):
        grip = "tight" if p > math.pi * dr else f"loose by {math.pi * dr - p:.0f} mm"
        t(cx, cy + r + 30 + 20 * i, f"{name}: {math.pi * d:.0f} around before ({math.pi * d - p:.0f} spare), "
          f"{math.pi * dr:.0f} shrunk ({grip})", 14, "middle", fill=col)
    return p


out.append('<defs><marker id="a" viewBox="0 0 10 10" refX="5" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
           '<path d="M0,0 L10,5 L0,10 z" fill="#222"/></marker></defs>')
out.append(f'<rect width="{W}" height="{H}" fill="#fff"/>')
t(30, 42, 'W55RP20-EVB-Pico in clear 2:1 heat shrink, before shrinking', 22, weight="bold")
t(30, 68, 'Alpha Wire FIT-221: blue 1-1/2" (F22111/2, 38.1 to 19.05 mm), orange 1" (F2211IN, 25.4 to 12.7 mm). Circles are the tubes as supplied. Scale 6:1, mm.', 14, fill="#555")
pj = section(310, 330, jack, "Jack end (RJ45)",
             [(JACK_Y, 0, JACK_TOP, "#c9c9c9"), ((JACK_Y[0] + 0.6, JACK_Y[1] - 0.6), PIN_Z, -BOARD_T, "#d4af37")])
pt = section(870, 330, tail, "Thin end (USB-C)", [(USB_Y, 0, USB_TOP, "#bbbbbb")])

# side view
y0, sx = 690, 9.0
x0 = 590 - LENGTH * sx / 2 + 4.75 * sx
tube_l = 90.0
out.append(f'<rect x="{x0 - 4.75*sx - (tube_l-LENGTH)/2*sx:.1f}" y="{y0 - 92}" width="0" height="0" fill="none"/>')
t(30, y0 - 120, "Side view (scale 9:1 along the board)", 18, weight="bold")
bx = lambda x: x0 + x * sx
out.append(f'<rect x="{bx(0):.1f}" y="{y0}" width="{80.0*sx:.1f}" height="{BOARD_T*sx:.1f}" fill="#2e6b3a"/>')
out.append(f'<rect x="{bx(-4.75):.1f}" y="{y0 - JACK_TOP*sx/2.2:.1f}" width="{21.8*sx:.1f}" height="{JACK_TOP*sx/2.2:.1f}" fill="#c9c9c9" stroke="#555"/>')
out.append(f'<rect x="{bx(80.6-7.35):.1f}" y="{y0 - USB_TOP*sx/2.2:.1f}" width="{7.35*sx:.1f}" height="{USB_TOP*sx/2.2:.1f}" fill="#bbb" stroke="#555"/>')
tx0, tx1 = bx(-4.75 - (tube_l - LENGTH) / 2), bx(80.6 + (tube_l - LENGTH) / 2)
out.append(f'<rect x="{tx0:.1f}" y="{y0 - 80}" width="{tx1-tx0:.1f}" height="100" rx="10" fill="#4a7fb5" fill-opacity="0.12" stroke="#4a7fb5" stroke-width="2"/>')
out.append(f'<line x1="{bx(-4.75):.1f}" y1="{y0+48}" x2="{bx(80.6):.1f}" y2="{y0+48}" stroke="#222" marker-start="url(#a)" marker-end="url(#a)"/>')
t((bx(-4.75) + bx(80.6)) / 2, y0 + 68, f"{LENGTH:.1f} jack face to USB-C mouth", 13, "middle")
out.append(f'<line x1="{tx0:.1f}" y1="{y0-96}" x2="{tx1:.1f}" y2="{y0-96}" stroke="#4a7fb5" marker-start="url(#a)" marker-end="url(#a)"/>')
t((tx0 + tx1) / 2, y0 - 102, f"cut {tube_l:.0f} mm of tube; 4 ft makes {int(1219 // tube_l)}", 13, "middle", fill="#4a7fb5")
t(bx(-4.75), y0 - 62, "RJ45", 13, "start")
t(bx(80.6), y0 - 34, "USB-C", 13, "end")
t(30, H - 26, "Vertical sizes in the side view are not to scale. Dimensions from WIZnet's STEP, via ../evb-fdm/evb_fdm.py.", 12, fill="#777")

svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica, Arial, sans-serif">' + "".join(out) + "</svg>"
open("evb_shrink_fit.svg", "w").write(svg)
print(f"jack wrap {pj:.1f} mm, tail wrap {pt:.1f} mm")
