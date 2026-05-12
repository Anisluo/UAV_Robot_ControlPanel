#!/usr/bin/env python3
"""Replace selected part_NN.stl files with their convex-hull equivalents.

Why: the "EPISODE 1" text in the source STEP file is baked into the
geometry as embossed/engraved features on one specific solid.  Editing
the STEP to remove it would require OpenCascade boolean surgery; the
cheap equivalent is to replace the offending part's triangle mesh with
the convex hull of its own vertices, which erases all recessed /
raised detail while preserving the outline.

Usage:
    # Simplify specific parts (by id) to their convex hull:
    python simplify_parts.py --parts 0,1

    # Simplify every part:
    python simplify_parts.py --all

    # Restore the original tessellated STLs (re-runs step_to_stl.py):
    python simplify_parts.py --restore

The script preserves `arm_model.json`; only the STL payloads are
overwritten.
"""
import argparse
import pathlib
import struct
import subprocess
import sys

import numpy as np
from scipy.spatial import ConvexHull


HERE = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = HERE.parent
ASSETS_DIR = PROJECT_ROOT / "assets" / "arm_model"


def read_stl_vertices(path: pathlib.Path) -> np.ndarray:
    """Return an (N, 3) float32 array of all triangle vertices."""
    data = path.read_bytes()
    n_tri = struct.unpack("<I", data[80:84])[0]
    expected = 84 + n_tri * 50
    if len(data) < expected:
        raise RuntimeError(f"STL truncated: {path.name}")
    verts = np.empty((n_tri * 3, 3), dtype=np.float32)
    off = 84
    for i in range(n_tri):
        # Each triangle: 12 bytes normal + 36 bytes three verts + 2 pad
        t = struct.unpack_from("<12f", data, off + 12)
        verts[i * 3 + 0] = t[0:3]
        verts[i * 3 + 1] = t[3:6]
        verts[i * 3 + 2] = t[6:9]
        off += 50
    return verts


def hull_to_stl(points: np.ndarray, out_path: pathlib.Path) -> int:
    """Compute the 3D convex hull of `points` and write a binary STL."""
    # scipy returns triangles as index triples with outward-facing order.
    hull = ConvexHull(points)
    with open(out_path, "wb") as f:
        f.write(b"simplified (convex hull)".ljust(80, b" "))
        f.write(struct.pack("<I", len(hull.simplices)))
        for a, b, c in hull.simplices:
            p0, p1, p2 = points[a], points[b], points[c]
            e1, e2 = p1 - p0, p2 - p0
            n = np.cross(e1, e2)
            m = float(np.linalg.norm(n)) or 1.0
            n = (n / m).astype(np.float32)
            f.write(struct.pack("<3f", *n))
            f.write(struct.pack("<3f", *p0))
            f.write(struct.pack("<3f", *p1))
            f.write(struct.pack("<3f", *p2))
            f.write(struct.pack("<H", 0))
    return len(hull.simplices)


def list_parts():
    return sorted(p for p in ASSETS_DIR.glob("part_*.stl"))


def simplify(ids: list[int]):
    parts = list_parts()
    for p in parts:
        idx = int(p.stem.split("_")[1])
        if idx not in ids:
            continue
        verts = read_stl_vertices(p)
        before = len(verts) // 3
        after = hull_to_stl(verts, p)
        print(f"[simplify] {p.name}: {before} → {after} triangles")


def restore():
    converter = HERE / "step_to_stl.py"
    if not converter.exists():
        sys.exit(f"missing {converter}")
    print("[simplify] re-tessellating from STEP ...")
    subprocess.check_call([sys.executable, str(converter)])


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--parts", help="comma-separated list of part ids (e.g. 0,1,3)")
    g.add_argument("--all", action="store_true", help="simplify every part")
    g.add_argument("--restore", action="store_true",
                   help="re-run step_to_stl.py to restore originals")
    args = ap.parse_args()

    if args.restore:
        restore(); return

    if args.all:
        ids = [int(p.stem.split("_")[1]) for p in list_parts()]
    else:
        ids = [int(x) for x in args.parts.split(",") if x.strip()]
    simplify(ids)
    print(f"[simplify] done. {len(ids)} part(s) replaced.")


if __name__ == "__main__":
    main()
