"""Buffered OSM road surfaces, sampled in the scene's local metric frame.

All geometry in this module uses east/north/up coordinates. The exporter is
responsible for rotating the completed scene into glTF's Y-up coordinates.
"""

from __future__ import annotations

from collections import Counter, defaultdict
import logging
import math
import re
from typing import Callable

import numpy as np
from shapely.geometry import box
from shapely.geometry.base import BaseGeometry
import trimesh
from trimesh.visual.material import PBRMaterial
from trimesh.visual.texture import TextureVisuals


LOGGER = logging.getLogger(__name__)

ROAD_WIDTHS = {
    "motorway": 12.0,
    "motorway_link": 5.0,
    "trunk": 10.0,
    "trunk_link": 5.0,
    "primary": 8.0,
    "primary_link": 5.0,
    "secondary": 7.0,
    "secondary_link": 5.0,
    "tertiary": 6.0,
    "tertiary_link": 4.5,
    "residential": 5.5,
    "living_street": 4.0,
    "unclassified": 5.0,
    "service": 3.5,
    "track": 3.0,
    "pedestrian": 4.0,
    "footway": 1.8,
    "path": 1.5,
    "cycleway": 2.5,
    "bridleway": 2.0,
    "steps": 1.5,
}

SURFACE_COLORS = {
    "asphalt": [62, 65, 69, 255],
    "gravel": [145, 136, 117, 255],
    "dirt": [133, 108, 75, 255],
    "paving_stones": [160, 151, 136, 255],
}


def parse_width(value: object) -> float | None:
    """Read an OSM length in metres, including feet/inches when specified."""
    if value is None:
        return None
    raw = str(value).strip().lower().replace(",", ".")
    metric = re.fullmatch(r"(?:~\s*)?(\d+(?:\.\d+)?)\s*(?:m|metres?|meters?)?", raw)
    if metric:
        number = float(metric.group(1))
        return number if math.isfinite(number) and number > 0 else None
    imperial = re.fullmatch(
        r"(\d+(?:\.\d+)?)\s*(?:ft|feet|foot|')"
        r"(?:\s*(\d+(?:\.\d+)?)\s*(?:in|inches?|\"))?",
        raw,
    )
    if imperial:
        number = float(imperial.group(1)) * 0.3048
        number += float(imperial.group(2) or 0) * 0.0254
        return number if math.isfinite(number) and number > 0 else None
    return None


def road_width(tags: dict, lane_width_m: float = 3.0) -> float:
    """Resolve width using explicit width, lanes, then highway class."""
    explicit = parse_width(tags.get("width"))
    if explicit is not None:
        return explicit
    try:
        lanes = float(str(tags.get("lanes", "")).split(";")[0])
    except (ValueError, TypeError):
        lanes = 0.0
    if math.isfinite(lanes) and lanes > 0:
        return lanes * lane_width_m
    return ROAD_WIDTHS.get(tags.get("highway"), 4.0)


def road_category(tags: dict) -> str:
    """Group OSM highway values into readable scene statistics."""
    highway = tags.get("highway", "unclassified")
    if highway in {"motorway", "motorway_link", "trunk", "trunk_link", "primary", "primary_link"}:
        return "major"
    if highway in {"secondary", "secondary_link", "tertiary", "tertiary_link"}:
        return "secondary"
    if highway in {"residential", "living_street", "unclassified"}:
        return "residential"
    if highway == "service":
        return "service"
    if highway == "track":
        return "track"
    return "footway"


def road_surface(tags: dict) -> str:
    """Resolve the four reusable road material categories from OSM tags."""
    surface = str(tags.get("surface", "")).lower().split(";")[0].strip()
    if surface in {"gravel", "fine_gravel", "pebblestone", "compacted", "rock"}:
        return "gravel"
    if surface in {"dirt", "earth", "ground", "mud", "grass", "grass_paver", "sand", "unpaved", "woodchips"}:
        return "dirt"
    if surface in {"paving_stones", "sett", "cobblestone", "cobblestone:flattened", "unhewn_cobblestone", "bricks"}:
        return "paving_stones"
    if surface in {"asphalt", "paved", "concrete", "concrete:plates", "concrete:lanes", "chipseal", "metal", "wood"}:
        return "asphalt"
    if tags.get("highway") in {"track", "path", "bridleway"}:
        return "dirt"
    if tags.get("highway") in {"pedestrian", "steps"}:
        return "paving_stones"
    return "asphalt"


def _parts(geometry: BaseGeometry, geom_type: str):
    if geometry.is_empty:
        return
    if geometry.geom_type == geom_type:
        yield geometry
    elif hasattr(geometry, "geoms"):
        for part in geometry.geoms:
            yield from _parts(part, geom_type)


def _cells(polygon: BaseGeometry, spacing: float):
    """Clip a surface to a metric grid before triangulation.

    Triangulating a kilometre-long road and then uniformly subdividing creates
    many tiny, thin triangles. Vertical strips followed by occupied cells keep
    work proportional to road length and also preserve concavities and holes.
    Every output triangle is at most sqrt(2) * spacing metres along an edge.
    """
    xmin, ymin, xmax, ymax = polygon.bounds
    for column in range(math.floor(xmin / spacing), math.ceil(xmax / spacing)):
        left = column * spacing
        strip = polygon.intersection(box(left, ymin - 1, left + spacing, ymax + 1))
        for component in _parts(strip, "Polygon"):
            cymin, cymax = component.bounds[1], component.bounds[3]
            for row in range(math.floor(cymin / spacing), math.ceil(cymax / spacing)):
                bottom = row * spacing
                cell = component.intersection(box(left, bottom, left + spacing, bottom + spacing))
                for part in _parts(cell, "Polygon"):
                    if part.area > 1e-8:
                        yield part


def _surface_mesh(polygon: BaseGeometry, elevation: Callable, spacing: float, offset: float):
    vertices_parts = []
    faces_parts = []
    vertex_count = 0
    for component in _parts(polygon, "Polygon"):
        for cell in _cells(component, spacing):
            xy, faces = trimesh.creation.triangulate_polygon(cell, engine="earcut")
            if not len(faces):
                continue
            vertices_parts.append(xy)
            faces_parts.append(faces + vertex_count)
            vertex_count += len(xy)
    if not vertices_parts:
        return None
    xy = np.vstack(vertices_parts)
    z = np.broadcast_to(np.asarray(elevation(xy[:, 0], xy[:, 1]), dtype=float), (len(xy),))
    if not np.all(np.isfinite(z)):
        raise ValueError("Terrain returned non-finite elevations for road vertices")
    mesh = trimesh.Trimesh(
        vertices=np.column_stack((xy, z + offset)),
        faces=np.vstack(faces_parts),
        process=False,
    )
    _raise_over_terrain(mesh, elevation, offset)
    # Face winding should be consistent even if the clipping library returns
    # clockwise rings. Roads are one-sided surfaces viewed from above.
    downward = mesh.face_normals[:, 2] < 0
    if np.any(downward):
        mesh.faces[downward] = mesh.faces[downward, ::-1]
    return mesh


def _raise_over_terrain(mesh: trimesh.Trimesh, elevation: Callable, offset: float) -> None:
    """Keep road interiors above terrain triangle bends, sharing seam corrections.

    A road triangle may cross a terrain ridge despite all its vertices clearing
    the ground. Probe edge midpoints and the centroid, then lift each incident
    vertex enough to keep these points at the requested offset. Duplicate XY
    vertices on cell boundaries receive the same correction to avoid cracks.
    """
    triangles = mesh.triangles
    probes = np.stack((triangles.mean(axis=1), (triangles[:, 0] + triangles[:, 1]) / 2,
                       (triangles[:, 1] + triangles[:, 2]) / 2,
                       (triangles[:, 2] + triangles[:, 0]) / 2), axis=1)
    ground = np.asarray(elevation(probes[:, :, 0], probes[:, :, 1]))
    if not np.all(np.isfinite(ground)):
        raise ValueError("Terrain returned non-finite elevations for road interiors")
    needed = np.maximum(0, (ground + offset - probes[:, :, 2]).max(axis=1))
    if np.max(needed) < 1e-8:
        return
    # Clip computations may differ below floating point export precision.
    xy = mesh.vertices[:, :2].astype(np.float32)
    _, inverse = np.unique(xy, axis=0, return_inverse=True)
    correction = np.zeros(inverse.max() + 1)
    np.maximum.at(correction, inverse[mesh.faces].ravel(), np.repeat(needed, 3))
    mesh.vertices[:, 2] += correction[inverse]


def add_roads(scene: trimesh.Scene, features: list, elevation: Callable, config: dict) -> dict:
    """Add roads from local-XY LineString/MultiLineString features to a scene.

    ``elevation(x, y)`` must return local Z for NumPy coordinate arrays (or a
    broadcastable scalar). Prefer the terrain mesh's piecewise-linear sampler
    so the roads align with the exported terrain, including resampled slopes.
    Configuration lives under ``roads``; optional top-level ``_clip_polygon``
    or ``_clip_bounds`` constrains the buffered edges to the terrain footprint.
    """
    settings = config.get("roads", {})
    spacing = float(settings.get("sample_spacing_m", 10.0))
    offset = float(settings.get("offset_m", 0.2))
    lane_width = float(settings.get("lane_width_m", 3.0))
    if not math.isfinite(spacing) or spacing <= 0:
        raise ValueError("roads.sample_spacing_m must be a positive finite number")
    if not math.isfinite(offset) or not math.isfinite(lane_width) or lane_width <= 0:
        raise ValueError("roads.offset_m must be finite and lane_width_m must be positive")
    clipping = config.get("_clip_polygon")
    if clipping is None and config.get("_clip_bounds") is not None:
        clipping = box(*config["_clip_bounds"])
    groups = defaultdict(list)
    categories = Counter()
    materials = Counter()
    source_ids = defaultdict(list)
    segment_count = 0
    skipped_count = 0
    for index, feature in enumerate(features):
        tags = feature.get("tags", {})
        highway = tags.get("highway")
        geometry = feature.get("geometry")
        if geometry is None or highway not in ROAD_WIDTHS:
            skipped_count += 1
            continue
        width = road_width(tags, lane_width)
        material_name = road_surface(tags)
        accepted = False
        for line in _parts(geometry, "LineString"):
            if line.length < 0.01:
                continue
            # Flat ends avoid round bumps at adjacent OSM way endpoints.
            buffered = line.buffer(width / 2, cap_style=2, join_style=1, quad_segs=3)
            if clipping is not None:
                buffered = buffered.intersection(clipping)
            if buffered.is_empty:
                continue
            mesh = _surface_mesh(buffered, elevation, spacing, offset)
            if mesh is None:
                continue
            groups[material_name].append(mesh)
            source_ids[material_name].append(str(feature.get("id", index)))
            categories[road_category(tags)] += 1
            materials[material_name] += 1
            segment_count += 1
            accepted = True
        if not accepted:
            skipped_count += 1

    triangle_count = 0
    for material_name, meshes in sorted(groups.items()):
        mesh = trimesh.util.concatenate(meshes)
        mesh.visual = TextureVisuals(material=PBRMaterial(
            name=f"road_{material_name}",
            baseColorFactor=SURFACE_COLORS[material_name],
            metallicFactor=0.0,
            roughnessFactor=0.95,
            doubleSided=True,
        ))
        mesh.metadata.update({
            "kind": "road",
            "surface": material_name,
            "source": "OpenStreetMap",
            "osm_ids": source_ids[material_name],
        })
        scene.add_geometry(mesh, geom_name=f"roads_{material_name}", node_name=f"roads_{material_name}")
        triangle_count += len(mesh.faces)
    LOGGER.info("Created %d road segments in %d material meshes", segment_count, len(groups))
    return {
        "road_segments": segment_count,
        "road_meshes": len(groups),
        "road_triangles": triangle_count,
        "road_materials": dict(materials),
        "road_categories": dict(categories),
        "roads_skipped": skipped_count,
    }
