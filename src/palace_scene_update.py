"""Apply the palace override to an existing town without regenerating its area."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile

import numpy as np
from shapely.geometry import Polygon
import trimesh

from .buildings import add_buildings
from .config import coordinate_frame
from .glb import read_glb, pack_glb
from .map_publication import publish_files
from .navmesh_preparation import prepare_navmesh
from .palace_floorplan import definition
from .road_scene_update import _refresh_map_hash
from .scene_mesh_update import replace_scene_meshes
from .tree_map import load_tree_map
from .verify import inspect_glb


def prepare_palace_patch(original, terrain, config):
    """Replace only this palace's meshes, using the published terrain datum."""
    data = definition()
    name = "building_" + data["osm_id"].replace("/", "_") + "_0"
    document, _ = read_glb(original)
    old_wall = next((m for m in document["meshes"] if m.get("name") == name + "_walls"), None)
    if old_wall is None:
        raise ValueError("The selected map does not contain the mapped Gödöllő palace")
    frame = coordinate_frame(config)
    points = np.array([frame["transformer"].transform(*p) for p in data["reference_footprint_lonlat"]])
    points -= frame["origin"]
    polygon = Polygon(points)
    # Older and already-patched town walls use the same source ENU metre frame.
    # Refuse a changed footprint rather than applying a stale geographic trace.
    positions = document["accessors"][old_wall["primitives"][0]["attributes"]["POSITION"]]
    if not (np.allclose(positions["min"][:2], points.min(axis=0), atol=.02, rtol=0) and
            np.allclose(positions["max"][:2], points.max(axis=0), atol=.02, rtol=0)):
        raise ValueError("Existing palace footprint differs from the reference outline")
    scene = trimesh.Scene()
    stats = add_buildings(scene, [{"id": data["osm_id"], "geometry": polygon,
                                  "tags": {"building": "yes", "building:levels": "2"}}], terrain, config)
    if stats["buildings"] != 1:
        raise ValueError("Palace geometry preparation failed")
    updated, binary = replace_scene_meshes(original, scene, (name + "_",))
    metadata = updated["scenes"][updated.get("scene", 0)].setdefault("extras", {})
    metadata["palace_interior"] = {"version": 1, "source": data["sources"],
                                   "storeys": 2, "exterior_doors": stats["doors"], "windows": stats["windows"]}
    updated["buffers"][0]["byteLength"] = len(binary)
    return pack_glb(updated, binary), metadata["palace_interior"]


def update_palace(map_path: Path, config: dict, companions=()):
    """Validate and back up the map, navigation and matching placement companions.

    Trees, poles, vehicles and guardrails can retain their positions because the
    terrain and every other building stay unchanged. Refresh only companions
    proven to match the exact original map; navigation is rebaked separately.
    """
    map_path = Path(map_path).resolve()
    terrain, _, _ = load_tree_map(map_path, config)
    original = map_path.read_bytes()
    old_hash = hashlib.sha256(original).hexdigest()
    patched, stats = prepare_palace_patch(original, terrain, config)
    new_hash = hashlib.sha256(patched).hexdigest()
    originals = {map_path: original}
    report_path = map_path.with_name("report.json")
    report_bytes = report_path.read_bytes() if report_path.exists() else None
    originals[report_path] = report_bytes
    report = json.loads(report_bytes) if report_bytes else {}
    with tempfile.TemporaryDirectory(prefix=".palace-", dir=map_path.parent) as directory:
        staging = Path(directory)
        pending = staging / map_path.name
        pending.write_bytes(patched)
        stats["verification"] = inspect_glb(pending)
        after, _, _ = load_tree_map(pending, config)
        if not (np.array_equal(terrain.heights, after.heights) and np.array_equal(terrain.x_axis, after.x_axis)
                and np.array_equal(terrain.y_axis, after.y_axis) and terrain.datum == after.datum):
            raise ValueError("Palace update changed the published terrain")
        report["navigation"] = prepare_navmesh(pending, staging / map_path.with_suffix(".navmesh").name)
        changes = {map_path: pending}
        for companion in companions:
            path = Path(companion).resolve()
            if path in originals:
                raise ValueError("Duplicate palace-update companion")
            content = path.read_bytes()
            originals[path] = content
            document, binary = read_glb(content) if path.suffix == ".glb" else (json.loads(content), None)
            if not _refresh_map_hash(document, old_hash, new_hash):
                raise ValueError(f"No original map fingerprint in {path}")
            prepared = pack_glb(document, binary) if binary is not None else (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode()
            # Keep nested companion destinations distinct in the staging area.
            staged = staging / f"companion-{len(changes)}{path.suffix}"
            staged.write_bytes(prepared)
            changes[path] = staged
        report.update(palace_interior=stats, update="palace_interior", verification=stats["verification"])
        (staging / report_path.name).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        changes[report_path] = staging / report_path.name
        for path in staging.iterdir():
            if path.name in (map_path.with_suffix(".navmesh").name, map_path.with_suffix(".navigation.json").name,
                             map_path.with_suffix(".navmesh.json").name):
                destination = map_path.parent / path.name
                originals[destination] = destination.read_bytes() if destination.exists() else None
                changes[destination] = path
        for path, content in originals.items():
            if (path.read_bytes() if path.exists() else None) != content:
                raise ValueError(f"File changed during palace preparation: {path}")
        backup = map_path.parent / "palace-backups" / old_hash
        for path, content in originals.items():
            if content is None:
                continue
            saved = backup / path.relative_to(map_path.parent)
            saved.parent.mkdir(parents=True, exist_ok=True)
            if saved.exists() and saved.read_bytes() != content:
                raise ValueError("Refusing to overwrite a differing palace backup")
            if not saved.exists():
                saved.write_bytes(content)
        publish_files(changes)
    return {"map_sha256": new_hash, "backup": str(backup), **stats}
