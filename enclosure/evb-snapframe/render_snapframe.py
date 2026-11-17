"""Writes docs/img/evb_snapframe_sheet.png: two views of the case and a section through a clip."""
import os

import cadquery as cq
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
top = cq.importers.importStep(os.path.join(HERE, "evb-snapframe-top.step")).val()
bot = cq.importers.importStep(os.path.join(HERE, "evb-snapframe-bottom.step")).val()
CLEAR, YELLOW = np.array((0.70, 0.84, 0.95)), np.array((0.95, 0.77, 0.10))


def tris(shape, dz=0.0):
    v, t = shape.tessellate(0.05, 0.2)
    p = np.array([(a.x, a.y, a.z + dz) for a in v])
    return p[np.array(t)]


def view(parts, elev, azim, px=900, mm=125.0):
    """Orthographic z-buffer render: parts are (triangles, colour)."""
    e, a = np.radians(elev), np.radians(azim)
    fwd = np.array([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)])    # towards the viewer
    right = np.array([-np.sin(a), np.cos(a), 0.0])
    up = np.cross(fwd, right)
    light = np.array([0.35, -0.45, 0.82]); light /= np.linalg.norm(light)
    allp = np.concatenate([t.reshape(-1, 3) for t, _ in parts])
    c = (allp.min(0) + allp.max(0)) / 2
    h = int(px * 0.7)
    img, zb = np.ones((h, px, 3)), np.full((h, px), -1e9)
    s = px / mm
    for tri, rgb in parts:
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        q = tri - c
        u, v, w = q @ right * s + px / 2, h / 2 - q @ up * s, q @ fwd
        for i in range(len(tri)):
            x0, x1 = int(max(u[i].min(), 0)), int(min(u[i].max() + 1, px - 1))
            y0, y1 = int(max(v[i].min(), 0)), int(min(v[i].max() + 1, h - 1))
            if x1 < x0 or y1 < y0:
                continue
            X, Y = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
            ax_, bx, cx = u[i]
            ay, by, cy = v[i]
            d = (by - cy) * (ax_ - cx) + (cx - bx) * (ay - cy)
            if abs(d) < 1e-9:
                continue
            l0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / d
            l1 = ((cy - ay) * (X - cx) + (ax_ - cx) * (Y - cy)) / d
            l2 = 1 - l0 - l1
            z = l0 * w[i, 0] + l1 * w[i, 1] + l2 * w[i, 2]
            m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0) & (z > zb[y0:y1 + 1, x0:x1 + 1])
            zb[y0:y1 + 1, x0:x1 + 1][m] = z[m]
            nn = n[i] if n[i] @ fwd > 0 else -n[i]
            img[y0:y1 + 1, x0:x1 + 1][m] = rgb * (0.5 + 0.5 * max(nn @ light, 0))
    return img


def section(ax, shape, color, x):
    plane = cq.Workplane("YZ", origin=(x, 0, 0)).rect(120, 80).extrude(0.001).val()
    for f in shape.intersect(plane).Faces():
        if abs(f.normalAt().x) < 0.9 or f.Center().x > x + 0.0005:
            continue
        v, t = f.tessellate(0.01, 0.1)
        for a, b, c in t:
            ax.fill([v[a].y, v[b].y, v[c].y], [v[a].z, v[b].z, v[c].z], color=color, lw=0.2, ec=color)


if __name__ == "__main__":
    fig = plt.figure(figsize=(14, 10))
    for i, (parts, el, az) in enumerate((([(tris(bot), YELLOW), (tris(top), CLEAR)], 42, -70),
                                         ([(tris(bot, -9), YELLOW), (tris(top, 9), CLEAR)], 35, -125))):
        ax = fig.add_subplot(2, 2, i + 1)
        ax.imshow(view(parts, el, az)); ax.set_axis_off()
    for i, lim in enumerate((None, (-4.5, 0.5, -5.5, 5.5))):
        ax = fig.add_subplot(2, 2, i + 3)
        section(ax, bot, YELLOW, 12.0); section(ax, top, CLEAR, 12.0)
        ax.set_aspect("equal"); ax.grid(alpha=0.3)
        ax.set_title("Snap detail (mm)" if lim else "Section at the front snaps, x = 12 (mm)")
        if lim:
            ax.set_xlim(*lim[:2]); ax.set_ylim(*lim[2:])
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "..", "..", "docs", "img", "evb_snapframe_sheet.png"), dpi=110)
