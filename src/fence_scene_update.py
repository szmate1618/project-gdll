"""Update fences in a published town without reconstructing its buildings."""
from __future__ import annotations

from datetime import datetime, timezone
import json
import logging
from pathlib import Path
import shutil
import tempfile
import time

import numpy as np
import trimesh

from .fences import add_fences
from .glb import pack_glb, read_glb
from .scene_mesh_update import replace_scene_meshes
from .map_publication import publish_files
from .navmesh_preparation import prepare_navmesh
from .osm import parse_fences
from .tree_map import load_tree_map
from .verify import inspect_glb

LOG = logging.getLogger(__name__)


def prepare_fence_patch(original: bytes, fences: trimesh.Scene, stats: dict, provenance: dict) -> bytes:
    """Append replacement fences while retaining every original town byte."""
    document, binary = replace_scene_meshes(original, fences, ("fences_",))
    metadata = document["scenes"][document.get("scene", 0)]["extras"]
    metadata["fences"] = stats
    metadata.setdefault("sources", {})["fences"] = provenance
    document["buffers"][0]["byteLength"] = len(binary)
    return pack_glb(document, binary)


def update_fences(map_path: Path, config: dict, osm_path: Path, provenance: dict) -> dict:
    """Replace fence meshes, retaining the published terrain and town graph.

    Validate the candidate GLB and unchanged terrain before publication. Keep
    original map/report backups and roll back if companion publication fails.
    Bake matching navigation before replacing the map or its companions.
    Tree and forest manifests remain separate and need refreshing afterwards.
    """
    started = time.monotonic()
    map_path = Path(map_path).resolve()
    terrain, frame, map_info = load_tree_map(map_path, config)
    original = map_path.read_bytes()
    payload = json.loads(Path(osm_path).read_text(encoding="utf-8"))
    features = parse_fences(payload, frame["transformer"], frame["origin"], frame["clip"])
    fence_scene = trimesh.Scene()
    stats = add_fences(fence_scene, features, terrain, {**config, "_clip_polygon": frame["clip"]})
    LOG.info("Appending fence buffers while preserving the published town binary")
    patched = prepare_fence_patch(original, fence_scene, stats, provenance)
    document, _ = read_glb(patched)
    metadata = document["scenes"][document.get("scene", 0)]["extras"]
    report_path = map_path.with_name("report.json")
    report_original = report_path.read_bytes() if report_path.exists() else None
    report = json.loads(report_original) if report_original is not None else dict(metadata)
    report.update(fences=stats, sources=metadata["sources"])
    pending = {}
    navigation_staging = tempfile.TemporaryDirectory(prefix=".fences-nav-", dir=map_path.parent)

    def stage(path, data):
        with tempfile.NamedTemporaryFile(prefix=".fences-", suffix=path.suffix,
                                         dir=path.parent, delete=False) as stream:
            stream.write(data)
            return Path(stream.name)

    try:
        pending[map_path] = stage(map_path, patched)
        LOG.info("Validating the complete candidate GLB and unchanged terrain")
        verification = inspect_glb(pending[map_path])
        updated_terrain, _, _ = load_tree_map(pending[map_path], config)
        if (not np.array_equal(terrain.heights, updated_terrain.heights)
                or not np.array_equal(terrain.x_axis, updated_terrain.x_axis)
                or not np.array_equal(terrain.y_axis, updated_terrain.y_axis)
                or terrain.datum != updated_terrain.datum):
            raise ValueError("Fence update changed the published terrain; refusing to publish")
        LOG.info("Rebuilding zombie navigation for the updated fences")
        navigation_directory = Path(navigation_staging.name)
        report["navigation"] = prepare_navmesh(pending[map_path], navigation_directory / map_path.with_suffix(".navmesh").name)
        pending.update({map_path.parent / path.name: path for path in navigation_directory.iterdir()})
        report.update(verification=verification, created_utc=datetime.now(timezone.utc).isoformat(),
                      elapsed_seconds=round(time.monotonic() - started, 2), update="fences_only")
        pending[report_path] = stage(report_path, (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode())
        if (map_path.read_bytes() != original
                or (report_path.read_bytes() if report_path.exists() else None) != report_original):
            raise ValueError("Map or report changed during fence preparation; retry when other work completes")
        backup = map_path.parent / "fence-backups" / map_info["map_sha256"]
        backup.mkdir(parents=True, exist_ok=True)
        for path, data in [(map_path, original), (report_path, report_original)]:
            if data is None:
                continue
            saved = backup / path.name
            if saved.exists() and saved.read_bytes() != data:
                raise ValueError("An existing fence backup differs; refusing to overwrite it")
            if not saved.exists():
                shutil.copy2(path, saved)
        publish_files(pending)
    finally:
        for temporary in pending.values():
            temporary.unlink(missing_ok=True)
        navigation_staging.cleanup()
    return report
