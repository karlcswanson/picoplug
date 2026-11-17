"""Assembly mockup: WIZnet's EVB-Pico in the sandwich case (yellow frame-bottom, clear
frame-top, aluminum plate). Everything is already in the case coordinates, so nothing moves.

    python assembly.py ../../ref/W55RP20-EVB-Pico_V100.step

Writes evb-sandwich-assembly.usdz and .obj/.mtl (colours per part), the same exploded
(evb-sandwich-exploded .usdz/.obj), evb-sandwich-purple.usdz with the purple PCB plate
(plate-pcb/), and a one-colour evb-sandwich-assembly.stl."""
import os
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
TOL, ANG = 0.05, 0.2

MATERIALS = {                      # name: (diffuse RGB, opacity)
    "clear": ((0.82, 0.90, 0.96), 0.35),
    "yellow": ((0.95, 0.77, 0.10), 1.0),
    "aluminum": ((0.78, 0.80, 0.83), 1.0),
    "pcb": ((0.10, 0.42, 0.22), 1.0),
    "metal": ((0.62, 0.64, 0.66), 1.0),
    "part": ((0.15, 0.15, 0.17), 1.0),
    "purple": ((0.30, 0.12, 0.45), 1.0),      # OSH Park mask
    "gold": ((0.90, 0.72, 0.30), 1.0),        # ENIG
    "silk": ((0.95, 0.95, 0.95), 1.0),
    "fr4": ((0.78, 0.70, 0.45), 1.0),
}
PLATE_Z = 14.55                    # UP_Z1 in evb_sandwich.py: underside of the top plate
CASE = [("frame-bottom", "yellow", -6.0), ("frame-top", "clear", 6.0), ("plate-top", "aluminum", 14.0)]


def evb_parts(step):
    """WIZnet's solids, coloured by shape: the board green, the RJ45 cage metal, the rest dark."""
    out = []
    for i, s in enumerate(cq.importers.importStep(step).solids().vals()):
        b = s.BoundingBox()
        if (b.zmax > 8 and b.xmin > 18) or (b.zmin < -3 and b.xlen < 1):
            continue            # pin headers in WIZnet's model; the board has none (same filter as the case)
        if b.xlen > 60 and b.ylen > 15 and b.zlen < 2.0:
            mat = "pcb"
        elif b.zlen > 10 and b.xlen > 15:
            mat = "metal"
        else:
            mat = "part"
        out.append((f"evb-{i}", mat, s, 0.0))
    return out


def pcb_plate(step):
    """The purple plate-pcb (plate-pcb/picoplug-plate.step, exported by kicad-cli with copper,
    mask and silk) at the top plate's height. Body FR4, copper gold, mask purple, silk white."""
    from OCP.TopoDS import TopoDS_Iterator

    def kids(sh):
        it = TopoDS_Iterator(sh)
        while it.More():
            c = cq.Shape.cast(it.Value())
            if c.ShapeType() in ("Compound", "CompSolid"):
                yield from kids(c.wrapped)
            else:
                yield c
            it.Next()
    out = []
    for i, k in enumerate(kids(cq.importers.importStep(step).val().wrapped)):
        b = k.BoundingBox()
        if k.ShapeType() == "Solid":
            mat = "fr4" if b.zlen > 1.0 and b.xlen > 80 else "gold"
        else:                       # flat faces: mask at 1.56 / -0.05, silk at 1.585 / -0.075
            mat = "silk" if b.zmax > 1.57 or b.zmin < -0.06 else "purple"
        out.append((f"pcbplate-{i}", mat, k.translate((0, 0, PLATE_Z)), 0.0))
    return out


def write_obj(path, parts):
    mtl = os.path.splitext(os.path.basename(path))[0] + ".mtl"
    with open(os.path.join(HERE, mtl), "w") as f:
        for name, (rgb, d) in MATERIALS.items():
            f.write(f"newmtl {name}\nKd {rgb[0]} {rgb[1]} {rgb[2]}\nKa 0 0 0\nKs 0.3 0.3 0.3\nNs 40\nd {d}\nillum 2\n\n")
    n = 0
    with open(path, "w") as f:
        f.write(f"mtllib {mtl}\n")
        for name, mat, shape, dz in parts:
            verts, tris = shape.tessellate(TOL, ANG)
            f.write(f"o {name}\nusemtl {mat}\n")
            for v in verts:
                f.write(f"v {v.x:.4f} {v.y:.4f} {v.z + dz:.4f}\n")
            for a, b, c in tris:
                f.write(f"f {a + 1 + n} {b + 1 + n} {c + 1 + n}\n")
            n += len(verts)


METAL = {"aluminum": (0.9, 0.35), "metal": (0.9, 0.3)}       # metallic, roughness


def write_usdz(path, parts):
    """USDZ with a UsdPreviewSurface per material (iPad Quick Look and most viewers show it).
    Solids are merged per material, so the EVB's 200-odd parts stay a handful of meshes."""
    import tempfile
    from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, UsdUtils
    merged = {}
    for name, mat, shape, dz in parts:
        key = name if not name.startswith(("evb-", "pcbplate-")) else name.split("-")[0] + "-" + mat
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


def main():
    step = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "../../ref/W55RP20-EVB-Pico_V100.step")
    case = [(name, mat, cq.importers.importStep(os.path.join(HERE, f"evb-sandwich-{name}.step")).val(), dz)
            for name, mat, dz in CASE]
    evb = evb_parts(step)
    write_obj(os.path.join(HERE, "evb-sandwich-assembly.obj"), [(n, m, s, 0.0) for n, m, s, _ in case] + evb)
    write_obj(os.path.join(HERE, "evb-sandwich-exploded.obj"), case + evb)
    write_usdz(os.path.join(HERE, "evb-sandwich-assembly.usdz"), [(n, m, s, 0.0) for n, m, s, _ in case] + evb)
    write_usdz(os.path.join(HERE, "evb-sandwich-exploded.usdz"), case + evb)
    plate_step = os.path.join(HERE, "plate-pcb", "picoplug-plate.step")
    if os.path.exists(plate_step):     # the same, with the purple PCB plate in place of the aluminum
        write_usdz(os.path.join(HERE, "evb-sandwich-purple.usdz"),
                   [(n, m, s, 0.0) for n, m, s, _ in case if n != "plate-top"] + pcb_plate(plate_step) + evb)
    allp = cq.Compound.makeCompound([s for _, _, s, _ in case + evb])
    cq.exporters.export(cq.Workplane().add(allp), os.path.join(HERE, "evb-sandwich-assembly.stl"),
                        tolerance=TOL, angularTolerance=ANG)
    bb = allp.BoundingBox()
    print(f"{len(evb)} EVB solids; assembly {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm")


if __name__ == "__main__":
    main()
