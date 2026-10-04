"""Terrain-following OSM railway beds in local east/north/up metres."""

from __future__ import annotations

from collections import Counter
import logging
import math
from typing import Callable

from shapely.geometry import box
import trimesh
from trimesh.visual.material import PBRMaterial
from trimesh.visual.texture import TextureVisuals

from .osm import RAILWAY_TYPES
from .roads import parse_width
from .terrain_surfaces import geometry_parts, surface_mesh


LOG = logging.getLogger(__name__)


def add_railways(scene: trimesh.Scene, features: list, elevation: Callable, config: dict) -> dict:
    """Add buffered railway centerlines with one shared gray ballast material.

    Each OSM way represents a mapped track; ``tracks`` and ``passenger_lines``
    tags do not multiply its width. A usable OSM ``width`` overrides the default
    ``railways.width_m``. Track beds follow the rendered terrain sampler, with
    the configured clearance in metres; bridge and tunnel structures are not
    inferred from path tags. Top-level ``_clip_polygon`` or ``_clip_bounds``
    constrains buffered edges to the terrain footprint.
    """
    settings = config.get("railways", {})
    width = float(settings.get("width_m", 4.0))
    spacing = float(settings.get("sample_spacing_m", 8.0))
    offset = float(settings.get("offset_m", 0.3))
    if not math.isfinite(width) or width <= 0:
        raise ValueError("railways.width_m must be a positive finite number")
    if not math.isfinite(spacing) or spacing <= 0:
        raise ValueError("railways.sample_spacing_m must be a positive finite number")
    if not math.isfinite(offset) or offset < 0:
        raise ValueError("railways.offset_m must be a nonnegative finite number")
    clipping = config.get("_clip_polygon")
    if clipping is None and config.get("_clip_bounds") is not None:
        clipping = box(*config["_clip_bounds"])

    meshes = []
    source_ids = []
    categories = Counter()
    segment_count = 0
    skipped_count = 0
    for index, feature in enumerate(features):
        tags = feature.get("tags", {})
        category = tags.get("railway")
        geometry = feature.get("geometry")
        if geometry is None or category not in RAILWAY_TYPES:
            skipped_count += 1
            continue
        track_width = parse_width(tags.get("width")) or width
        accepted = False
        for line in geometry_parts(geometry, "LineString"):
            if line.length < 0.01:
                continue
            # Flat ends let adjoining mapped ways meet without end-cap bumps.
            buffered = line.buffer(track_width / 2, cap_style=2, join_style=1, quad_segs=3)
            if clipping is not None:
                buffered = buffered.intersection(clipping)
            if buffered.is_empty:
                continue
            mesh = surface_mesh(buffered, elevation, spacing, offset)
            if mesh is None:
                continue
            meshes.append(mesh)
            source_ids.append(str(feature.get("id", index)))
            categories[category] += 1
            segment_count += 1
            accepted = True
        if not accepted:
            skipped_count += 1

    triangle_count = 0
    if meshes:
        mesh = trimesh.util.concatenate(meshes)
        mesh.visual = TextureVisuals(material=PBRMaterial(
            name="railway_ballast",
            baseColorFactor=[107, 111, 116, 255],
            metallicFactor=0.0,
            roughnessFactor=0.95,
            doubleSided=True,
        ))
        mesh.metadata.update({
            "kind": "railway",
            "source": "OpenStreetMap",
            "osm_ids": source_ids,
            "railway_categories": dict(categories),
        })
        scene.add_geometry(mesh, geom_name="railways_ballast", node_name="railways_ballast")
        triangle_count = len(mesh.faces)
    LOG.info("Created %d railway segments in %d ballast meshes", segment_count, int(bool(meshes)))
    return {
        "railway_segments": segment_count,
        "railway_meshes": int(bool(meshes)),
        "railway_triangles": triangle_count,
        "railway_categories": dict(categories),
        "railways_skipped": skipped_count,
    }
