"""Lower a published town's road offset without rebuilding its geometry."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import tempfile

import numpy as np

from .glb import pack_glb, read_glb
from .tree_map import load_tree_map
from .verify import inspect_glb


def prepare_road_offset_patch(original: bytes, old_offset: float, new_offset: float) -> bytes:
    """Translate road leaf nodes along world Y; retain every mesh/binary byte.

    Offsets are metres above terrain. Existing ridge-clearance corrections are
    retained. The caller supplies the source offset for older maps which did
    not record it. Generated towns use matrix/translation nodes, not TRS rigs.
    """
    if not all(math.isfinite(x) and x >= .01 for x in (old_offset, new_offset)):
        raise ValueError("Road offsets must be finite and at least one centimetre")
    document, binary = read_glb(original)
    if document.get("skins") or document.get("animations"):
        raise ValueError("Road offset updates require a static town")
    scene = document["scenes"][document.get("scene", 0)]
    metadata = scene.setdefault("extras", {}).setdefault("roads", {})
    if not math.isclose(metadata.get("offset_m", old_offset), old_offset, abs_tol=1e-6):
        raise ValueError("Recorded road offset differs from the supplied source offset")
    nodes, meshes = document["nodes"], document["meshes"]
    stack = [(i, np.eye(4)) for i in scene["nodes"]]
    visited = set()
    changed = 0
    while stack:
        index, parent = stack.pop()
        if index in visited:
            raise ValueError("Road updates require an unshared, acyclic scene graph")
        visited.add(index)
        node = nodes[index]
        if "rotation" in node or "scale" in node:
            raise ValueError("Road updates require generated matrix/translation nodes")
        local = np.array(node.get("matrix", np.eye(4).ravel(order="F")), dtype=float).reshape(4, 4, order="F")
        if "translation" in node:
            if "matrix" in node:
                raise ValueError("Node cannot have both a matrix and translation")
            local[:3, 3] = node["translation"]
        if not np.isfinite(local).all():
            raise ValueError("Non-finite scene transform")
        mesh = meshes[node["mesh"]] if "mesh" in node else {}
        road = mesh.get("name", "").startswith("roads_") and mesh.get("extras", {}).get("kind") == "road"
        if road:
            if node.get("children"):
                raise ValueError("Road nodes must be leaves to preserve other geometry")
            local[:3, 3] += (np.linalg.inv(parent) @ [0, new_offset - old_offset, 0, 0])[:3]
            node.pop("translation", None)
            node["matrix"] = local.ravel(order="F").tolist()
            changed += 1
        stack.extend((child, parent @ local) for child in node.get("children", []))
    if not changed:
        raise ValueError("No generated road nodes found")
    metadata.update(offset_m=new_offset)
    return pack_glb(document, binary)


def _refresh_map_hash(document, old_hash, new_hash):
    """Refresh only references proven to belong to the exact original town."""
    count = 0
    if isinstance(document, dict):
        for key, value in document.items():
            if key == "map_sha256":
                if value != old_hash:
                    raise ValueError("Companion has a stale or unrelated map fingerprint")
                document[key] = new_hash
                count += 1
            else:
                count += _refresh_map_hash(value, old_hash, new_hash)
    elif isinstance(document, list):
        for item in document:
            count += _refresh_map_hash(item, old_hash, new_hash)
    return count


def update_road_offset(map_path: Path, config: dict, old_offset: float, companions=()) -> dict:
    """Validate, back up and publish a road-only patch and matching companions.

    Companion geometry/placements stay intact: roads move only vertically and
    terrain, buildings and road footprints are unchanged. Fingerprint guards
    remain enabled. Every candidate is staged before any publication; ordinary
    publication errors restore previously published files from their originals.
    """
    map_path = Path(map_path).resolve()
    terrain, _, _ = load_tree_map(map_path, config)
    original = map_path.read_bytes()
    old_hash = hashlib.sha256(original).hexdigest()
    patched = prepare_road_offset_patch(original, old_offset, config["roads"]["offset_m"])
    new_hash = hashlib.sha256(patched).hexdigest()
    originals, candidates = {map_path: original}, {map_path: patched}
    for companion in companions:
        path = Path(companion).resolve()
        if path in originals:
            raise ValueError("Duplicate map/companion path")
        data = path.read_bytes()
        if path.suffix == ".glb":
            document, binary = read_glb(data)
        else:
            document, binary = json.loads(data), None
        if not _refresh_map_hash(document, old_hash, new_hash):
            raise ValueError(f"No map fingerprint in companion: {path}")
        originals[path] = data
        candidates[path] = (pack_glb(document, binary) if binary is not None else
                            (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode())
    report_path = map_path.with_name("report.json")
    report_original = report_path.read_bytes() if report_path.exists() else None
    report = json.loads(report_original) if report_original else {}
    report.setdefault("roads", {})["offset_m"] = config["roads"]["offset_m"]
    report["update"] = "road_offset_only"
    staged = {}

    def stage(path, data):
        with tempfile.NamedTemporaryFile(prefix=".road-offset-", suffix=path.suffix,
                                         dir=path.parent, delete=False) as stream:
            stream.write(data)
            return Path(stream.name)

    try:
        for path, data in candidates.items():
            staged[path] = stage(path, data)
        verification = inspect_glb(staged[map_path])
        updated, _, _ = load_tree_map(staged[map_path], config)
        if (not np.array_equal(terrain.heights, updated.heights)
                or not np.array_equal(terrain.x_axis, updated.x_axis)
                or not np.array_equal(terrain.y_axis, updated.y_axis)
                or terrain.datum != updated.datum):
            raise ValueError("Road patch changed the published terrain")
        report["verification"] = verification
        originals[report_path] = report_original
        staged[report_path] = stage(report_path, (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode())
        for path, data in originals.items():
            if (path.read_bytes() if path.exists() else None) != data:
                raise ValueError(f"File changed during road preparation: {path}")
        backup = map_path.parent / "road-offset-backups" / old_hash
        backup.mkdir(parents=True, exist_ok=True)
        for index, (path, data) in enumerate(originals.items()):
            if data is None:
                continue
            saved = backup / f"{index}-{path.name}"
            if saved.exists() and saved.read_bytes() != data:
                raise ValueError("Refusing to overwrite a differing road backup")
            if not saved.exists():
                saved.write_bytes(data)
        published = []
        try:
            for path, temporary in staged.items():
                temporary.replace(path)
                published.append(path)
        except Exception:
            for path in reversed(published):
                if originals[path] is None:
                    path.unlink(missing_ok=True)
                else:
                    stage(path, originals[path]).replace(path)
            raise
    finally:
        for temporary in staged.values():
            temporary.unlink(missing_ok=True)
    return {"map_sha256": new_hash, "backup": str(backup), "offset_m": config["roads"]["offset_m"]}
