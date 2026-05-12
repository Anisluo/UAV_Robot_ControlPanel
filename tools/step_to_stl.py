#!/usr/bin/env python3
"""Convert steps/episode.STEP (a SolidWorks 2024 assembly export) into a
set of per-solid binary STL files + a metadata JSON that HostGUI's 3D
viewer loads at runtime.

The assembly has 13 solids and no explicit URDF.  Since we don't have
the original joint-frame data, this script picks a *heuristic*
kinematic chain: sort solids by Z-centroid (ascending) and split into 7
buckets (base + 6 movable joints).  The resulting grouping gives a
reasonable "whole arm spins when J1 moves, upper half when J2 moves"
visualisation; tweak `JOINT_BUCKET_EDGES` in this file if you want to
re-assign parts by hand.

Run once; commit the output under `assets/arm_model/`.

Dependencies:
    pip install cadquery-ocp        # OpenCascade Python bindings (~200 MB)
"""
import json
import os
import pathlib
import struct
import sys

try:
    from OCP.STEPControl import STEPControl_Reader
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopAbs import TopAbs_SOLID
    from OCP.TopoDS import TopoDS
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.BRep import BRep_Tool
    from OCP.TopLoc import TopLoc_Location
    from OCP.BRepBndLib import BRepBndLib
    from OCP.Bnd import Bnd_Box
except ImportError:
    sys.exit("pip install cadquery-ocp (needed for STEP reading)")


# ── Paths ────────────────────────────────────────────────────────────────
HERE = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = HERE.parent               # UAV_Robot_ControlPanel/
STEP_PATH = PROJECT_ROOT / "steps" / "episode.STEP"
OUT_DIR  = PROJECT_ROOT / "assets" / "arm_model"

# ── Mesh tessellation detail ────────────────────────────────────────────
# Deflection is the max distance between the curved surface and its
# triangulated approximation (in the STEP's own units — SolidWorks
# defaults to millimetres). 0.5 mm is smooth enough for visualisation
# without producing giant STL files.
TESS_LINEAR_DEFLECTION  = 0.5
TESS_ANGULAR_DEFLECTION = 0.5


# ── Joint axes (ZYYZYZ) — default for a 6-DOF articulated arm. If the
#    episode model doesn't match this, edit the list; the viewer applies
#    these axes directly so no re-tesselation is needed.
JOINT_AXES = [
    [0.0, 0.0, 1.0],   # J1 base yaw       (Z)
    [0.0, 1.0, 0.0],   # J2 shoulder pitch (Y)
    [0.0, 1.0, 0.0],   # J3 elbow pitch    (Y)
    [1.0, 0.0, 0.0],   # J4 forearm roll   (X)
    [0.0, 1.0, 0.0],   # J5 wrist pitch    (Y)
    [1.0, 0.0, 0.0],   # J6 gripper roll   (X)
]

# Manual part-to-bucket override. Bucket 0 = stationary base; bucket
# k (1..6) = parts that follow joint Jk.  Auto Z-clustering scattered
# horizontal-extension parts arbitrarily, so we hand-assign by id below.
JOINT_BUCKET_OVERRIDE = {
    0: 0, 12: 0,
    1: 1, 2: 1,                 # column body + J2 motor housing stay with J1
    3: 2,                       # upper column rotates with J2
    4: 3, 5: 3, 6: 3, 7: 3,     # elbow + horizontal start + brackets follow J3
    9: 4,                       # forearm roll
    8: 5,                       # wrist pitch
    10: 6, 11: 6,               # gripper
}

# Manual joint origins (mm, model coords). The auto bucket-mean centroid
# is poor for any joint whose bucket spans a large arm-extension volume.
# Set to None to fall back to auto.
JOINT_ORIGIN_OVERRIDE = [
    [  0.0,  0.0, 100.0],   # J1: central Z axis at top of base
    [  0.0,  0.0, 265.0],   # J2: pitch axle at top of motor housing (~part_02 z)
    [  0.0,  0.0, 410.0],   # J3: elbow at top of vertical column
    [200.0,  0.0, 421.0],   # J4: forearm roll along horizontal arm
    [260.0,  0.0, 421.0],   # J5: wrist pitch
    [290.0,  0.0, 421.0],   # J6: gripper roll
]


def read_solids(step_path: pathlib.Path):
    reader = STEPControl_Reader()
    status = reader.ReadFile(str(step_path))
    if int(status) != 1:
        sys.exit(f"STEP read failed: status={status}")
    reader.TransferRoots()
    shape = reader.OneShape()
    solids = []
    exp = TopExp_Explorer(shape, TopAbs_SOLID)
    while exp.More():
        solids.append(TopoDS.Solid_s(exp.Current()))
        exp.Next()
    return solids


def solid_bbox(solid):
    """Return (xmin, ymin, zmin, xmax, ymax, zmax)."""
    box = Bnd_Box()
    BRepBndLib.Add_s(solid, box, True)
    xmin, ymin, zmin, xmax, ymax, zmax = box.Get()
    return (xmin, ymin, zmin, xmax, ymax, zmax)


def centroid(bbox):
    x1, y1, z1, x2, y2, z2 = bbox
    return ((x1 + x2) * 0.5, (y1 + y2) * 0.5, (z1 + z2) * 0.5)


def tessellate(solid):
    """Trigger triangulation on the solid's faces in place."""
    BRepMesh_IncrementalMesh(solid, TESS_LINEAR_DEFLECTION, False,
                              TESS_ANGULAR_DEFLECTION, True)


def iter_triangles(solid):
    """Yield (v0, v1, v2, normal) tuples in global model space."""
    from OCP.TopAbs import TopAbs_FACE
    exp = TopExp_Explorer(solid, TopAbs_FACE)
    while exp.More():
        face = TopoDS.Face_s(exp.Current())
        loc  = TopLoc_Location()
        tri  = BRep_Tool.Triangulation_s(face, loc)
        if tri is not None:
            trsf = loc.Transformation()
            # Orientation sign: reversed faces → flip triangle winding.
            reversed_ = face.Orientation() == 1  # TopAbs_REVERSED
            for i in range(1, tri.NbTriangles() + 1):
                t = tri.Triangle(i)
                n1, n2, n3 = t.Get()
                if reversed_:
                    n2, n3 = n3, n2
                p1 = tri.Node(n1).Transformed(trsf)
                p2 = tri.Node(n2).Transformed(trsf)
                p3 = tri.Node(n3).Transformed(trsf)
                v1 = (p1.X(), p1.Y(), p1.Z())
                v2 = (p2.X(), p2.Y(), p2.Z())
                v3 = (p3.X(), p3.Y(), p3.Z())
                # Compute normal
                ux, uy, uz = (v2[0]-v1[0], v2[1]-v1[1], v2[2]-v1[2])
                vx, vy, vz = (v3[0]-v1[0], v3[1]-v1[1], v3[2]-v1[2])
                nx = uy*vz - uz*vy
                ny = uz*vx - ux*vz
                nz = ux*vy - uy*vx
                m  = (nx*nx + ny*ny + nz*nz) ** 0.5 or 1.0
                yield v1, v2, v3, (nx/m, ny/m, nz/m)
        exp.Next()


def write_binary_stl(path: pathlib.Path, triangles):
    """Write a binary STL to `path`. `triangles` is iterable of
    (v0, v1, v2, normal)."""
    with open(path, "wb") as f:
        f.write(b" " * 80)                # 80-byte header
        f.write(struct.pack("<I", 0))     # tri count placeholder
        n = 0
        for v0, v1, v2, normal in triangles:
            f.write(struct.pack("<3f", *normal))
            f.write(struct.pack("<3f", *v0))
            f.write(struct.pack("<3f", *v1))
            f.write(struct.pack("<3f", *v2))
            f.write(struct.pack("<H", 0))
            n += 1
        f.seek(80)
        f.write(struct.pack("<I", n))
    return n


def cluster_by_height(solids_info, n_buckets):
    """Return one bucket index per solid, sorted by Z-centroid.

    We split evenly by rank (not by value) so a heavy cluster of small
    parts near the gripper still gets distributed. This is a heuristic —
    expect to hand-tweak for the final release.
    """
    order = sorted(range(len(solids_info)),
                   key=lambda i: solids_info[i]["centroid"][2])
    bucket_of = [0] * len(solids_info)
    for rank, idx in enumerate(order):
        # Integer bucket by rank; floor division keeps earlier (lower)
        # solids in base buckets, later ones in tip buckets.
        bucket = (rank * n_buckets) // len(solids_info)
        bucket_of[idx] = min(bucket, n_buckets - 1)
    return bucket_of


def main():
    if not STEP_PATH.exists():
        sys.exit(f"STEP file not found: {STEP_PATH}")
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[step_to_stl] reading {STEP_PATH}")
    solids = read_solids(STEP_PATH)
    print(f"[step_to_stl] {len(solids)} solids")

    info = []
    for i, solid in enumerate(solids):
        bbox = solid_bbox(solid)
        c    = centroid(bbox)
        info.append({"solid": solid, "bbox": bbox, "centroid": c})

    n_buckets = len(JOINT_AXES) + 1  # base + 6 joints
    auto_buckets = cluster_by_height(info, n_buckets)
    buckets = [JOINT_BUCKET_OVERRIDE.get(i, auto_buckets[i])
               for i in range(len(info))]

    parts_meta = []
    for i, item in enumerate(info):
        solid = item["solid"]
        print(f"[step_to_stl] tessellating solid {i}")
        tessellate(solid)
        tri_count = write_binary_stl(OUT_DIR / f"part_{i:02d}.stl",
                                      iter_triangles(solid))
        parts_meta.append({
            "id":        i,
            "stl":       f"part_{i:02d}.stl",
            "centroid":  list(item["centroid"]),
            "bbox":      list(item["bbox"]),
            "triangles": tri_count,
            # Bucket 0 = base (stationary); 1..6 = joint index J1..J6.
            "joint":     buckets[i],
        })
        print(f"    -> part_{i:02d}.stl  "
              f"bucket={buckets[i]}  "
              f"tri={tri_count}  "
              f"c=({item['centroid'][0]:.1f},"
              f"{item['centroid'][1]:.1f},"
              f"{item['centroid'][2]:.1f})")

    # Compute the overall scene bounding box and the average centroid of
    # bucket 0 (=base) — the viewer uses these for camera framing and
    # joint-origin approximation.
    xmin = min(p["bbox"][0] for p in parts_meta)
    ymin = min(p["bbox"][1] for p in parts_meta)
    zmin = min(p["bbox"][2] for p in parts_meta)
    xmax = max(p["bbox"][3] for p in parts_meta)
    ymax = max(p["bbox"][4] for p in parts_meta)
    zmax = max(p["bbox"][5] for p in parts_meta)

    # Joint origins: rotation centre for each joint. Without a URDF we
    # estimate from geometry:
    #   * Z-axis (yaw) joint -> snap rotation centre onto the scene's
    #     vertical column (X,Y from base centroid). Otherwise the whole
    #     arm would pivot around the centre of its end-effector cluster
    #     and visibly translate when the base motor turns.
    #   * X / Y axis (pitch / roll) joint -> use the centroid of the
    #     parts driven by this joint — puts the centre near its motor.
    #   * Z height: top of the bucket immediately below this joint.
    base_parts = [p for p in parts_meta if p["joint"] == 0]
    # The base column's central axis matters more than its mean: a small
    # off-centre lug (wire-tie / flange) would drag the mean sideways
    # and make J1 rotate around the wrong line. Use the largest base
    # part (by bbox volume) as the column proxy.
    def _bbox_vol(p):
        x1,y1,z1,x2,y2,z2 = p["bbox"]
        return (x2-x1) * (y2-y1) * (z2-z1)
    if base_parts:
        column  = max(base_parts, key=_bbox_vol)
        base_cx = column["centroid"][0]
        base_cy = column["centroid"][1]
    else:
        base_cx = base_cy = 0.0

    joint_origins = []
    for j in range(len(JOINT_AXES)):
        axis  = JOINT_AXES[j]
        above = [p for p in parts_meta if p["joint"] >  j] or parts_meta
        below = [p for p in parts_meta if p["joint"] <= j] or parts_meta
        z_top_below = max(p["bbox"][5] for p in below)

        is_yaw_axis = (abs(axis[2]) > 0.9 and abs(axis[0]) < 0.1
                        and abs(axis[1]) < 0.1)
        if is_yaw_axis:
            cx, cy = base_cx, base_cy
            cz     = z_top_below
        else:
            cx = sum(p["centroid"][0] for p in above) / len(above)
            cy = sum(p["centroid"][1] for p in above) / len(above)
            cz = z_top_below

        # Manual override wins — lets us fix joints whose auto origin is
        # off (typical for arms with horizontal extension where bucket
        # centroids drift far from the actual joint axle line).
        ov = JOINT_ORIGIN_OVERRIDE[j] if j < len(JOINT_ORIGIN_OVERRIDE) else None
        if ov is not None:
            cx, cy, cz = ov[0], ov[1], ov[2]
        joint_origins.append([cx, cy, cz])

    model = {
        "units":        "mm",
        "parts":        parts_meta,
        "joint_axes":   JOINT_AXES,
        "joint_origins": joint_origins,
        "scene_bbox":   [xmin, ymin, zmin, xmax, ymax, zmax],
        "source":       "episode.STEP",
        "note":         "Bucket 0 = stationary base; bucket k (1..6) "
                        "= parts that follow joint Jk.",
    }
    with open(OUT_DIR / "arm_model.json", "w", encoding="utf-8") as f:
        json.dump(model, f, indent=2)
    print(f"[step_to_stl] wrote {OUT_DIR / 'arm_model.json'}")
    print(f"[step_to_stl] scene bbox: "
          f"({xmin:.1f},{ymin:.1f},{zmin:.1f}) → "
          f"({xmax:.1f},{ymax:.1f},{zmax:.1f})")


if __name__ == "__main__":
    main()
