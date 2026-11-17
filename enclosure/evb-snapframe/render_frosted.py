"""Writes docs/img/evb_snapframe_frosted.png: the case with the board inside, the clear top drawn the
way clear PETG/PLA actually prints: frosted, so the board shows through blurred and dimmed.
An approximation (fixed transmission and blur, no thickness), not a photo."""
import os
import sys

import cadquery as cq
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "evb-sandwich"))
import assembly as A  # noqa: E402

STEP = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "ref", "W55RP20-EVB-Pico_V100.step")
COL = {"pcb": (0.10, 0.42, 0.22), "metal": (0.70, 0.72, 0.74), "part": (0.15, 0.15, 0.17)}
YELLOW, FROST = np.array((0.95, 0.77, 0.10)), np.array((0.93, 0.95, 0.97))
TRANS, BLUR = 0.40, 5          # share of the scene behind that shows through; blur radius in px


def tris(shape):
    v, t = shape.tessellate(0.05, 0.2)
    p = np.array([(a.x, a.y, a.z) for a in v])
    return p[np.array(t)]


def raster(parts, elev, azim, c, px, h, mm, below=None):
    e, a = np.radians(elev), np.radians(azim)
    fwd = np.array([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)])
    right = np.array([-np.sin(a), np.cos(a), 0.0])
    up = np.cross(fwd, right)
    light = np.array([0.35, -0.45, 0.82]); light /= np.linalg.norm(light)
    img, zb = np.ones((h, px, 3)), np.full((h, px), -1e9)
    s = px / mm
    for tri, rgb in parts:
        rgb = np.array(rgb)
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
            (ax_, bx, cx), (ay, by, cy) = u[i], v[i]
            d = (by - cy) * (ax_ - cx) + (cx - bx) * (ay - cy)
            if abs(d) < 1e-9:
                continue
            l0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / d
            l1 = ((cy - ay) * (X - cx) + (ax_ - cx) * (Y - cy)) / d
            l2 = 1 - l0 - l1
            z = l0 * w[i, 0] + l1 * w[i, 1] + l2 * w[i, 2]
            m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0) & (z > zb[y0:y1 + 1, x0:x1 + 1])
            if below is not None:
                m &= z < below[y0:y1 + 1, x0:x1 + 1] - 0.05
            zb[y0:y1 + 1, x0:x1 + 1][m] = z[m]
            nn = n[i] if n[i] @ fwd > 0 else -n[i]
            img[y0:y1 + 1, x0:x1 + 1][m] = rgb * (0.55 + 0.45 * max(nn @ light, 0))
    return img, zb


def blur(img, r):
    out = img.copy()
    for _ in range(3):                        # three box passes, close to a gaussian
        k = 2 * r + 1
        p = np.pad(out, ((r, r), (r, r), (0, 0)), mode="edge")
        c = p.cumsum(0); c = np.concatenate([np.zeros_like(c[:1]), c], 0)
        out = (c[k:] - c[:-k])
        c = out.cumsum(1); c = np.concatenate([np.zeros_like(c[:, :1]), c], 1)
        out = (c[:, k:] - c[:, :-k]) / (k * k)
    return out


def frosted(scene, top, elev, azim, c, px=1100, mm=110):
    h = int(px * 0.62)
    s_img, s_z = raster(scene, elev, azim, c, px, h, mm)
    # peel the clear part's surfaces: every two surfaces in front of the scene is one wall
    t_img, t_z = raster(top, elev, azim, c, px, h, mm)
    count, z = np.zeros((h, px)), t_z
    for _ in range(6):
        count += z > s_z
        _, z = raster(top, elev, azim, c, px, h, mm, below=z)
    walls = np.ceil(count / 2)
    out = s_img.copy()
    for k in range(1, int(walls.max()) + 1):
        m = walls == k
        behind = blur(s_img, BLUR * k)
        t = TRANS ** k
        out[m] = (1 - t) * (t_img[m] / FROST.max()) * FROST + t * behind[m]
    return out


if __name__ == "__main__":
    top = cq.importers.importStep(os.path.join(HERE, "evb-snapframe-top.step")).val()
    bot = cq.importers.importStep(os.path.join(HERE, "evb-snapframe-bottom.step")).val()
    scene = [(tris(bot), YELLOW)] + [(tris(s), COL[mat]) for _, mat, s, _ in A.evb_parts(STEP)]
    toptri = [(tris(top), FROST)]
    c = np.array([(-4.75 + 80.6) / 2, 10.5, 5.0])
    views = [("Clear top, frosted as printed", 32, -58), ("Through the opening", 8, -90), ("From the USB end", 25, -20)]
    fig, axs = plt.subplots(len(views), 1, figsize=(11, 6.8 * len(views)))
    for ax, (title, e, a) in zip(axs, views):
        ax.imshow(np.clip(frosted(scene, toptri, e, a, c), 0, 1))
        ax.set_title(title, fontsize=12)
        ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "..", "..", "docs", "img", "evb_snapframe_frosted.png"), dpi=90)
