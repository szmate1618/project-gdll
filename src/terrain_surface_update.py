"""Regenerate road/path/railway overlays against the published terrain."""
from __future__ import annotations

from datetime import datetime, timezone
import json
import logging
from pathlib import Path
import tempfile
import time

import numpy as np
import trimesh

from .glb import pack_glb
from .map_publication import publish_files
from .navmesh_preparation import prepare_navmesh
from .osm import parse_osm, parse_railways
from .railway_texture import configure_track_sampler
from .railways import add_railways
from .roads import add_roads
from .scene_mesh_update import replace_scene_meshes
from .tree_map import load_tree_map
from .verify import inspect_glb

LOG = logging.getLogger(__name__)


def update_terrain_surfaces(map_path: Path, config: dict, osm_path: Path, provenance: dict) -> dict:
    """Validate map and navigation together before atomic publication.

    Building, fence and terrain buffers remain byte-identical, making this a
    useful controlled rendering/collision experiment. Vegetation preparation
    remains the generator's separate next stage, with its normal fingerprints.
    """
    started = time.monotonic()
    terrain, frame, _ = load_tree_map(map_path, config)
    original = map_path.read_bytes()
    payload = json.loads(osm_path.read_text(encoding="utf-8"))
    _, roads = parse_osm(payload, frame["transformer"], frame["origin"], frame["clip"])
    railways = parse_railways(payload, frame["transformer"], frame["origin"], frame["clip"])
    scene = trimesh.Scene()
    settings = {**config, "_clip_polygon": frame["clip"]}
    road_stats = add_roads(scene, roads, terrain, settings)
    railway_stats = add_railways(scene, railways, terrain, settings)
    document, binary = replace_scene_meshes(original, scene, ("roads_", "railways_"),
                                           tree_postprocessor=configure_track_sampler)
    metadata = document["scenes"][document.get("scene", 0)]["extras"]
    metadata.update(roads=road_stats, railways=railway_stats)
    metadata.setdefault("sources", {}).update(roads=provenance, railways=provenance)
    document["buffers"][0]["byteLength"] = len(binary)
    report_path = map_path.with_name("report.json")
    report_original = report_path.read_bytes() if report_path.exists() else None
    report = json.loads(report_original) if report_original else dict(metadata)
    report.update(roads=road_stats, railways=railway_stats, sources=metadata["sources"])
    with tempfile.TemporaryDirectory(prefix=".surfaces-", dir=map_path.parent) as scratch:
        staging = Path(scratch)
        pending = staging / map_path.name
        pending.write_bytes(pack_glb(document, binary))
        LOG.info("Validating regenerated surfaces and unchanged town geometry")
        verification = inspect_glb(pending)
        updated, _, _ = load_tree_map(pending, config)
        if not (np.array_equal(terrain.heights, updated.heights) and
                np.array_equal(terrain.x_axis, updated.x_axis) and
                np.array_equal(terrain.y_axis, updated.y_axis) and terrain.datum == updated.datum):
            raise ValueError("Surface regeneration changed the published terrain")
        report["navigation"] = prepare_navmesh(pending, staging / map_path.with_suffix(".navmesh").name)
        report.update(verification=verification, update="terrain_surfaces_only",
                      created_utc=datetime.now(timezone.utc).isoformat(),
                      elapsed_seconds=round(time.monotonic()-started, 2))
        (staging / report_path.name).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if (map_path.read_bytes() != original or
                (report_path.read_bytes() if report_path.exists() else None) != report_original):
            raise ValueError("Published map changed during surface regeneration")
        publish_files({map_path.parent / path.name: path for path in staging.iterdir()})
    return report
