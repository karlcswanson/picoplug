"""WIZnet's board model for the snap frame: loads it (without the pin headers in WIZnet's
model; the board ships without them) and writes colored USDZ previews of the case with the
board inside. Used by evb_snapframe.py and render_frosted.py; everything is in the case's
coordinates, so nothing moves."""
import os

import cadquery as cq

TOL, ANG = 0.05, 0.2

MATERIALS = {                      # name: (diffuse RGB, opacity)
    "clear": ((0.82, 0.90, 0.96), 0.35),
    "yellow": ((0.95, 0.77, 0.10), 1.0),
    "pcb": ((0.10, 0.42, 0.22), 1.0),
    "metal": ((0.62, 0.64, 0.66), 1.0),
    "part": ((0.15, 0.15, 0.17), 1.0),
}


def evb_parts(step):
    """WIZnet's solids, colored by shape: the board green, the RJ45 cage metal, the rest dark."""
    out = []
    for i, s in enumerate(cq.importers.importStep(step).solids().vals()):
        b = s.BoundingBox()
        if (b.zmax > 8 and b.xmin > 18) or (b.zmin < -3 and b.xlen < 1):
            continue            # pin headers in WIZnet's model; the board has none
        if b.xlen > 60 and b.ylen > 15 and b.zlen < 2.0:
            mat = "pcb"
        elif b.zlen > 10 and b.xlen > 15:
            mat = "metal"
        else:
            mat = "part"
        out.append((f"evb-{i}", mat, s, 0.0))
    return out


METAL = {"metal": (0.9, 0.3)}       # metallic, roughness


def write_usdz(path, parts):
    """USDZ with a UsdPreviewSurface per material (iPad Quick Look and most viewers show it).
    Solids are merged per material, so the EVB's 200-odd parts stay a handful of meshes."""
    import tempfile
    from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, UsdUtils
    merged = {}
    for name, mat, shape, dz in parts:
        key = name if not name.startswith("evb-") else "evb-" + mat
        pts, idx = merged.setdefault(key, (mat, [], []))[1:]
        verts, tris = shape.tessellate(TOL, ANG)
        n = len(pts)
        pts += [Gf.Vec3f(v.x, v.y, v.z + dz) for v in verts]
        idx += [i + n for t in tris for i in t]
    src = os.path.join(tempfile.mkdtemp(), os.path.splitext(os.path.basename(path))[0] + ".usdc")
    st = Usd.Stage.CreateNew(src)
    UsdGeom.SetStageUpAxis(st, UsdGeom.Tokens.z)
    UsdGeom.SetStageMetersPerUnit(st, 0.001)
    root = UsdGeom.Xform.Define(st, "/picoplug")
    st.SetDefaultPrim(root.GetPrim())
    mats = {}
    for name, (rgb, d) in MATERIALS.items():
        m = UsdShade.Material.Define(st, f"/picoplug/mat_{name}")
        sh = UsdShade.Shader.Define(st, f"/picoplug/mat_{name}/surface")
        sh.CreateIdAttr("UsdPreviewSurface")
        sh.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*rgb))
        sh.CreateInput("opacity", Sdf.ValueTypeNames.Float).Set(d)
        metal, rough = METAL.get(name, (0.0, 0.5 if d == 1 else 0.1))
        sh.CreateInput("metallic", Sdf.ValueTypeNames.Float).Set(metal)
        sh.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(rough)
        m.CreateSurfaceOutput().ConnectToSource(sh.ConnectableAPI(), "surface")
        mats[name] = m
    for key, (mat, pts, idx) in merged.items():
        mesh = UsdGeom.Mesh.Define(st, "/picoplug/" + key.replace("-", "_"))
        mesh.CreatePointsAttr(pts)
        mesh.CreateFaceVertexCountsAttr([3] * (len(idx) // 3))
        mesh.CreateFaceVertexIndicesAttr(idx)
        mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        mesh.CreateDoubleSidedAttr(True)
        UsdShade.MaterialBindingAPI.Apply(mesh.GetPrim()).Bind(mats[mat])
    st.GetRootLayer().Save()
    UsdUtils.CreateNewUsdzPackage(src, path)

