"""Transactional publication of a single authored building and its companions."""
import hashlib
import json
from pathlib import Path
import tempfile
import numpy as np
from .glb import read_glb, pack_glb
from .map_publication import publish_files
from .navmesh_preparation import prepare_navmesh
from .road_scene_update import _refresh_map_hash
from .tree_map import load_tree_map
from .verify import inspect_glb


def update_authored_building(map_path: Path, config: dict, companions, prepare_patch, name):
    """Validate and back up the map, navigation and matching placement companions.

    Trees, poles, vehicles and guardrails can retain their positions because the
    terrain and every other building stay unchanged. Refresh only companions
    proven to match the exact original map; navigation is rebaked separately.
    """
    map_path = Path(map_path).resolve()
    terrain, _, _ = load_tree_map(map_path, config)
    original = map_path.read_bytes()
    old_hash = hashlib.sha256(original).hexdigest()
    patched, stats = prepare_patch(original, terrain, config)
    new_hash = hashlib.sha256(patched).hexdigest()
    originals = {map_path: original}
    report_path = map_path.with_name("report.json")
    report_bytes = report_path.read_bytes() if report_path.exists() else None
    originals[report_path] = report_bytes
    report = json.loads(report_bytes) if report_bytes else {}
    with tempfile.TemporaryDirectory(prefix=f".{name}-", dir=map_path.parent) as directory:
        staging = Path(directory)
        pending = staging / map_path.name
        pending.write_bytes(patched)
        stats["verification"] = inspect_glb(pending)
        after, _, _ = load_tree_map(pending, config)
        if not (np.array_equal(terrain.heights, after.heights) and np.array_equal(terrain.x_axis, after.x_axis)
                and np.array_equal(terrain.y_axis, after.y_axis) and terrain.datum == after.datum):
            raise ValueError("Authored building update changed the published terrain")
        report["navigation"] = prepare_navmesh(pending, staging / map_path.with_suffix(".navmesh").name)
        changes = {map_path: pending}
        for companion in companions:
            path = Path(companion).resolve()
            if path in originals:
                raise ValueError("Duplicate authored-building companion")
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
        report.update({name + "_interior": stats, "update": name + "_interior", "verification": stats["verification"]})
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
                raise ValueError(f"File changed during authored building preparation: {path}")
        backup = map_path.parent / (name + "-backups") / old_hash
        for path, content in originals.items():
            if content is None:
                continue
            saved = backup / path.relative_to(map_path.parent)
            saved.parent.mkdir(parents=True, exist_ok=True)
            if saved.exists() and saved.read_bytes() != content:
                raise ValueError("Refusing to overwrite a differing authored building backup")
            if not saved.exists():
                saved.write_bytes(content)
        publish_files(changes)
    return {"map_sha256": new_hash, "backup": str(backup), **stats}
