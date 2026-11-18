"""Writes docs/img/evb_snapframe_bed.png: how each part sits on the print bed."""
import os

import cadquery as cq
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import render_snapframe as R

HERE = R.HERE
BED = R.np.array((0.25, 0.26, 0.28))
bed = cq.Workplane("XY").box(150, 90, 2, centered=(True, True, False)).translate((38, 0, -2)).val()
bottom = R.bot.translate(cq.Vector(0, -10.5, 5.2))
top = R.top.rotate(cq.Vector(0, 0, 0), cq.Vector(1, 0, 0), 90).translate(cq.Vector(0, 8, 3.5 * 1))
top = top.translate(cq.Vector(0, 0, -top.BoundingBox().zmin))

fig, axs = plt.subplots(2, 1, figsize=(10, 11))
for ax, part, rgb, title, note in (
        (axs[0], bottom, R.YELLOW, "Print 1: evb-snapframe-bottom.stl",
         "Floor flat on the bed, open side up, snap tabs pointing up."),
        (axs[1], top, R.CLEAR, "Print 2: evb-snapframe-top.stl",
         "Standing on its long side, logo facing sideways, opening running straight up.")):
    ax.imshow(R.view([(R.tris(bed), BED), (R.tris(part), rgb)], 32, -62, px=1100, mm=175.0))
    ax.set_axis_off()
    ax.set_title(title, fontsize=15, fontweight="bold", loc="left")
    ax.text(0.0, -0.02, note, transform=ax.transAxes, fontsize=12, va="top")
fig.tight_layout()
fig.savefig(os.path.join(HERE, "..", "..", "docs", "img", "evb_snapframe_bed.png"), dpi=110)
