"""Bake runtime zombie navigation from the exported, Y-up town GLB.

The obstacle extraction follows the earlier ``buildZombiePathObstacles`` map
exporter: low-detail building walls and roofs get one conservative oriented
rectangle per building; each fence triangle gets its own deduplicated rectangle.
This deliberately fills building courtyards and concave footprints. Trees are
excluded because runtime Detour steering handles their separate trunk colliders.
Terrain contributes the planar X-east/Z-south bounds, not slope restrictions.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import time

import numpy as np
import shapely
import trimesh

from tools.bake_zombie_navmesh import bake, write_mesh
from tools.merge_zombie_navmesh import merge_file


def _building_group(name, metadata):
    if (not name.startswith("building_")
            or metadata.get("building_detail", {}).get("level") == "near"
            or name.endswith(("_detailed_walls", "_doors", "_windows"))):
        return None
    for suffix in ("_walls", "_roof"):
        if name.endswith(suffix):
            return name[:-len(suffix)]
    return None


def _footprint(points, kind):
    """Return [kind,cx,cz,ux,uz,hx,hz], with the exporter's 1 mm margin."""
    points = np.unique(points, axis=0)
    if len(points) > 2:
        hull = shapely.convex_hull(shapely.multipoints(points))
        points = shapely.get_coordinates(hull)
        if hull.geom_type == "Polygon":
            points = points[:-1]
    axis = np.array([1., 0.])
    best_area = float("inf")
    for edge in np.roll(points, -1, axis=0) - points:
        length = np.linalg.norm(edge)
        if length <= 1e-12:
            continue
        candidate = edge / length
        if candidate[0] < 0 or (candidate[0] == 0 and candidate[1] < 0):
            candidate = -candidate
        side = np.array([-candidate[1], candidate[0]])
        projected = points @ np.column_stack((candidate, side))
        area = float(np.prod(np.ptp(projected, axis=0)))
        if area < best_area:
            best_area, axis = area, candidate
    side = np.array([-axis[1], axis[0]])
    basis = np.column_stack((axis, side))
    projected = points @ basis
    middle = (projected.min(axis=0) + projected.max(axis=0)) * .5
    center = basis @ middle
    half_extent = np.max(np.abs((points - center) @ basis), axis=0) + .001
    return [kind, *center.tolist(), *axis.tolist(), *half_extent.tolist()]


def _navigation_scene(scene, model_name, model_bytes, map_sha256):
    terrain = []
    buildings = {}
    fences = set()
    for node in sorted(scene.graph.nodes_geometry):
        transform, geometry_name = scene.graph[node]
        mesh = scene.geometry[geometry_name]
        if not isinstance(mesh, trimesh.Trimesh):
            continue
        name = str(node)
        # Node names match runtime draws; geometry names cover reused meshes.
        if not name.startswith(("building_", "fences_")) and name != "terrain":
            name = str(geometry_name)
        building = _building_group(name, mesh.metadata)
        fence = name.startswith("fences_") or mesh.metadata.get("kind") == "fence"
        is_terrain = name == "terrain"
        if not (building or fence or is_terrain):
            continue
        vertices = trimesh.transform_points(mesh.vertices, transform)
        if not np.all(np.isfinite(vertices)):
            raise ValueError(f"Navigation source {name} has non-finite coordinates")
        if is_terrain:
            if len(vertices):
                terrain.append(vertices[:, [0, 2]])
            continue
        triangles = vertices[mesh.faces]
        area = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                       triangles[:, 2] - triangles[:, 0]), axis=1)
        triangles = triangles[area >= 1e-10][:, :, [0, 2]]
        if building:
            if len(triangles):
                buildings.setdefault(building, []).append(triangles.reshape(-1, 2))
        else:
            # Bounding the whole material batch would close every mapped gate.
            for triangle in triangles:
                fences.add(tuple(_footprint(triangle, 2)))
    if not terrain:
        raise ValueError("Navigation preparation requires an exported terrain mesh")
    points = np.concatenate(terrain)
    minimum, maximum = points.min(axis=0), points.max(axis=0)
    if np.any(minimum >= maximum):
        raise ValueError("Navigation terrain must have nonempty X/Z bounds")
    obstacles = [_footprint(np.concatenate(buildings[name]), 0) for name in sorted(buildings)]
    obstacles.extend(list(record) for record in sorted(fences))
    return {
        "model": model_name, "model_bytes": model_bytes, "map_sha256": map_sha256,
        "axes": {"X": "projected east", "Y": "up", "Z": "projected south"},
        "obstacle_counts": {"buildings": len(buildings), "fences": len(fences), "trees": 0},
        "geometry": {"buildings": "Conservative oriented rectangles around low-detail walls and roofs; courtyards and concavities are filled",
                     "fences": "Individual projected triangle rectangles; mapped openings retained",
                     "terrain": "Planar X/Z extent; terrain slope is not evaluated",
                     "trees": "Excluded; runtime Detour trunk steering"},
        "raster": {"minimum": minimum.tolist(), "maximum": maximum.tolist()},
        "obstacles": obstacles,
    }


def prepare_navmesh(map_path: Path, output_path: Path) -> dict:
    """Validate a max-four-vertex GDNAV02 mesh and prepare its two sidecars.

    Callers stage these three files with their candidate map before publication.
    The binary header hashes the exact ``.navigation.json`` bake input; metadata
    also records the exact GLB hash. No source caches or vegetation are loaded.
    """
    started = time.monotonic()
    map_path, output_path = Path(map_path), Path(output_path)
    with map_path.open("rb") as stream:
        map_sha256 = hashlib.file_digest(stream, "sha256").hexdigest()
        model_bytes = stream.seek(0, 2)
        stream.seek(0)
        scene = trimesh.load_scene(stream, file_type="glb", process=False, skip_materials=True)
    source = _navigation_scene(scene, output_path.with_suffix(".glb").name, model_bytes, map_sha256)
    source_data = (json.dumps(source, indent=2, allow_nan=False) + "\n").encode("utf-8")
    source_sha256 = hashlib.sha256(source_data).hexdigest()
    source_path = output_path.with_suffix(".navigation.json")
    sidecar_path = output_path.with_name(output_path.name + ".json")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    # Bake and merge both validate topology and obstacle coverage before any
    # existing destination is touched. The triangle format is an internal step.
    with tempfile.TemporaryDirectory(prefix=".navmesh-", dir=output_path.parent) as directory:
        staging = Path(directory)
        triangle_path = staging / "triangles.navmesh"
        vertices, triangles, neighbors, bounds, bake_metadata = bake(source)
        write_mesh(triangle_path, vertices, triangles, neighbors, bounds, source_sha256)
        staged_output = staging / output_path.name
        metadata = merge_file(triangle_path, staged_output, max_vertices=4)
        metadata.pop("input")
        metadata.update(source=source_path.name, source_sha256=source_sha256,
                        model=source["model"], model_bytes=model_bytes, map_sha256=map_sha256,
                        obstacle_counts=source["obstacle_counts"], geometry=source["geometry"],
                        bake=bake_metadata)
        staged_source = staging / source_path.name
        staged_source.write_bytes(source_data)
        staged_sidecar = staging / sidecar_path.name
        staged_sidecar.write_text(json.dumps(metadata, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        staged_source.replace(source_path)
        staged_sidecar.replace(sidecar_path)
        staged_output.replace(output_path)
    return {"output": output_path.name, "source": source_path.name, "metadata": sidecar_path.name,
            "map_sha256": map_sha256, "source_sha256": source_sha256, "format": metadata["format"],
            "vertices": metadata["vertices"], "source_triangles": metadata["source_triangles"],
            "polygons": metadata["polygons"], "max_vertices": metadata["max_vertices"],
            "components": metadata["validation"]["components"],
            "obstacle_counts": source["obstacle_counts"], "file_bytes": metadata["file_bytes"],
            "elapsed_seconds": round(time.monotonic() - started, 2)}
