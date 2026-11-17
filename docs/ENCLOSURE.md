# Building the case

The case files live in `enclosure/`. The STL files there are ready to print; this page is
for changing the case and generating them again. Design notes, print settings and every
tunable are in the README next to each case.

| Folder | What |
|---|---|
| `enclosure/evb-snapframe/` | The two-piece printed snap case. [README](../enclosure/evb-snapframe/README.md) |
| `enclosure/evb-sandwich/` | Base geometry the snap case builds on, and the board model loader. |
| `enclosure/evb-fdm/` | Shared board dimensions. |

The scripts find each other by relative path, so keep this layout.

![sheet](img/evb_snapframe_sheet.png)

## Setup

Needs [uv](https://docs.astral.sh/uv/). It installs Python 3.12 and CadQuery, matplotlib,
numpy and usd-core from `enclosure/pyproject.toml` and `uv.lock` on first run.

```
cd enclosure
uv sync
```

## Generate the case

From `enclosure/evb-snapframe`:

```
uv run evb_snapframe.py
```

This writes `evb-snapframe-top.stl`, `evb-snapframe-bottom.stl` and the matching `.step`
files. A good build prints:

```
top/bottom overlap 0.0 mm3
```

The top STL is already rotated to print on its long side; the bottom prints on its floor.

### With the board model

WIZnet's 3D model of the board is not in the repo. Download `W55RP20-EVB-Pico_V100.step`
from WIZnet's documentation for the W55RP20-EVB-Pico and pass its path:

```
uv run evb_snapframe.py path/to/W55RP20-EVB-Pico_V100.step
```

This also checks the case against the board and should print
`board overlap check: clear`. It writes two coloured previews with the board inside,
`evb-snapframe-assembly.usdz` and `evb-snapframe-exploded.usdz`, which open on an iPhone or
iPad. They contain WIZnet's model, so they are not committed.

## Renders

From `enclosure/evb-snapframe`, after generating the case:

```
uv run render_snapframe.py                                  # docs/img/evb_snapframe_sheet.png
uv run render_bed.py                                        # docs/img/evb_snapframe_bed.png
uv run render_frosted.py path/to/W55RP20-EVB-Pico_V100.step # docs/img/evb_snapframe_frosted.png
```

## Changing the case

The snap case's dimensions are constants at the top of
`enclosure/evb-snapframe/evb_snapframe.py` (`BARB`, `ARMS`, `TAB_ROUND`, `SLOT_SHIFT` and
others); its README says what each one does. Regenerate the STL and STEP files and the
renders, and commit them with the change. Old and new halves may not fit each other, so
print both from the same version.
