"""Inspect a GLB without a display or renderer: python -m src.verify."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

import numpy as np
import trimesh


def inspect_glb(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("GLB is too small to contain a scene.")
    magic, version, length = struct.unpack_from("<4sII", data)
    if magic != b"glTF" or version != 2 or length != len(data):
        raise ValueError("Invalid GLB 2.0 header or declared length.")
    cursor, chunks = 12, []
    while cursor < len(data):
        size, kind = struct.unpack_from("<I4s", data, cursor)
        if size % 4 or cursor + 8 + size > len(data):
            raise ValueError("Invalid GLB chunk size/alignment.")
        chunks.append((kind, data[cursor + 8:cursor + 8 + size]))
        cursor += 8 + size
    if cursor != len(data) or not chunks or chunks[0][0] != b"JSON":
        raise ValueError("GLB must begin with a complete JSON chunk.")
    doc = json.loads(chunks[0][1])
    if doc["asset"]["version"] != "2.0" or not doc.get("meshes"):
        raise ValueError("Missing glTF 2.0 meshes.")
    if any("uri" in buf for buf in doc.get("buffers", [])) or any("uri" in im for im in doc.get("images", [])):
        raise ValueError("Scene has external dependencies; expected a self-contained GLB.")
    binary = next((chunk for kind, chunk in chunks if kind == b"BIN\x00"), b"")
    for view in doc.get("bufferViews", []):
        if view.get("buffer", 0) != 0 or view.get("byteOffset", 0) + view["byteLength"] > len(binary):
            raise ValueError("Buffer view extends outside the embedded binary data.")
    if not doc.get("images"):
        raise ValueError("Terrain texture is not embedded.")
    scene = trimesh.load_scene(path, process=False)
    triangles = vertices = 0
    for name, mesh in scene.geometry.items():
        if not isinstance(mesh, trimesh.Trimesh) or len(mesh.faces) == 0:
            raise ValueError(f"{name}: expected a nonempty triangle mesh.")
        if not np.isfinite(mesh.vertices).all() or mesh.faces.min() < 0 or mesh.faces.max() >= len(mesh.vertices):
            raise ValueError(f"{name}: nonfinite coordinates or invalid face indices.")
        if np.any(mesh.area_faces < 1e-10):
            raise ValueError(f"{name}: degenerate triangles.")
        vertices += len(mesh.vertices)
        triangles += len(mesh.faces)
    if not np.isfinite(scene.bounds).all():
        raise ValueError("Nonfinite scene bounds.")
    materials = [material.get("name", "unnamed") for material in doc.get("materials", [])]
    if "terrain_sentinel_rgb" not in materials:
        raise ValueError("Terrain material is absent.")
    buildings_checked = 0
    for name, roof in scene.geometry.items():
        if not name.startswith("building_") or not name.endswith("_roof"):
            continue
        wall = scene.geometry.get(name[:-5] + "_walls")
        if wall is None:
            raise ValueError(f"{name}: matching building walls are missing.")
        solid = trimesh.util.concatenate([wall, roof])
        solid.merge_vertices(merge_tex=True, merge_norm=True)
        if not solid.is_watertight or not solid.is_winding_consistent or solid.volume <= 0:
            raise ValueError(f"{name}: building is not a closed solid with consistent outward winding.")
        buildings_checked += 1
    return {"valid": True, "glb_bytes": len(data), "meshes": len(scene.geometry),
            "vertices": vertices, "triangles": triangles, "materials": materials,
            "embedded_images": len(doc["images"]), "bounds_gltf_m": scene.bounds.tolist(),
            "extents_gltf_m": scene.extents.tolist(), "building_solids_checked": buildings_checked}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, default=Path(__file__).resolve().parents[1] / "output/godollo.glb")
    args = parser.parse_args()
    print(json.dumps(inspect_glb(args.path), indent=2))


if __name__ == "__main__":
    main()
