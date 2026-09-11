"""Terrain-seated OSM buildings with courtyard-aware flat and pitched roofs."""

from __future__ import annotations

from collections import Counter
import logging
import re

import numpy as np
from shapely import constrained_delaunay_triangles, make_valid
from shapely.errors import ShapelyError
from shapely.geometry import LineString, MultiPolygon, Polygon
from shapely.ops import split
import trimesh
from trimesh.visual.material import PBRMaterial

LOG = logging.getLogger(__name__)

WALL_MATERIAL = PBRMaterial(name="generic_building_wall", baseColorFactor=[204, 193, 167, 255], metallicFactor=0.0, roughnessFactor=0.92)
ROOF_MATERIAL = PBRMaterial(name="generic_roof", baseColorFactor=[132, 78, 62, 255], metallicFactor=0.0, roughnessFactor=0.88)
TYPE_HEIGHTS = {
    "house": 7.0, "detached": 7.0, "semidetached_house": 7.0, "terrace": 8.0,
    "residential": 9.0, "apartments": 15.0, "commercial": 10.0, "retail": 6.0,
    "office": 12.0, "industrial": 9.0, "warehouse": 8.0, "church": 18.0,
    "school": 10.0, "kindergarten": 6.0, "garage": 3.0, "garages": 3.0,
    "shed": 3.0, "roof": 3.0, "hut": 3.0, "civic": 12.0,
}
PITCHED_TYPES = {"house", "detached", "semidetached_house", "terrace", "residential", "church", "hut"}


def parse_length(value) -> float | None:
    """Read common OSM meter/feet measurements; ignore malformed values."""
    if value is None:
        return None
    value = str(value).strip().lower().replace(",", ".")
    # A semicolon is an OSM list separator. The first usable value is enough
    # for this approximation; units such as feet are converted to meters.
    value = value.split(";")[0].strip()
    match = re.fullmatch(r"([+]?(?:\d+(?:\.\d*)?|\.\d+))\s*(m|meters?|metres?|ft|feet|foot|')?(?:\s*(\d+(?:\.\d+)?)\s*\")?", value)
    if not match:
        return None
    length = float(match.group(1))
    if match.group(2) in ("ft", "feet", "foot", "'"):
        length = length * 0.3048 + float(match.group(3) or 0) * 0.0254
    return length if np.isfinite(length) and length > 0 else None


def building_height(tags: dict, config: dict) -> float:
    """Total estimated height, including a pitched roof when present."""
    explicit = parse_length(tags.get("height"))
    if explicit is not None:
        return float(np.clip(explicit, 0.5, 500.0))
    levels = parse_length(tags.get("building:levels"))
    if levels is not None:
        return float(np.clip(levels * config.get("floor_height_m", 3.0), 0.5, 500.0))
    return float(TYPE_HEIGHTS.get(tags.get("building"), config.get("default_height_m", 8.0)))


def _axes(polygon: Polygon):
    corners = np.asarray(polygon.minimum_rotated_rectangle.exterior.coords)[:4]
    edges = np.roll(corners, -1, axis=0) - corners
    lengths = np.linalg.norm(edges, axis=1)
    axis = edges[int(np.argmax(lengths))] / np.max(lengths)
    cross = np.array([-axis[1], axis[0]])
    center = corners.mean(axis=0)
    uv = (corners - center) @ np.column_stack([axis, cross])
    return center, axis, cross, np.max(np.abs(uv), axis=0)


def _roof_surface(polygon: Polygon, shape: str, base_z: float, roof_height: float):
    """Triangulate piecewise planar roofs; splitting keeps ridge lines sharp.

    A minimum rotated rectangle defines the ridge. For irregular footprints the
    planes are clipped to the real footprint, including every courtyard. Walls
    close any resulting raised boundary edges.
    """
    center, axis, cross, half = _axes(polygon)
    half_length, half_width = np.maximum(half, 0.01)
    extent = max(half) * 5.0 + 5.0
    cutlines = []
    if shape != "flat":
        cutlines.append(LineString([center - axis * extent, center + axis * extent]))
    if shape == "hipped":
        offset = half_length - half_width
        # End planes intersect the side planes along u +/- v = +/- offset.
        for sign in (-1, 1):
            for slope in (-1, 1):
                point = center + axis * (sign * offset)
                direction = axis + cross * slope
                cutlines.append(LineString([point - direction * extent, point + direction * extent]))
    pieces = [polygon]
    for cutline in cutlines:
        pieces = [part for piece in pieces for part in split(piece, cutline).geoms if isinstance(part, Polygon) and part.area > 1e-8]
    # Snap polygon rings to the exact coordinates glTF can represent *before*
    # triangulating. Snapping already triangulated faces can reverse a thin
    # triangle and break winding along its shared edge.
    quantized_pieces = []
    for piece in pieces:
        shell = np.asarray(piece.exterior.coords).astype(np.float32).astype(np.float64)
        holes = [np.asarray(ring.coords).astype(np.float32).astype(np.float64) for ring in piece.interiors]
        fixed = make_valid(Polygon(shell, holes))
        if isinstance(fixed, Polygon):
            quantized_pieces.append(fixed)
        else:
            quantized_pieces.extend(part for part in fixed.geoms if isinstance(part, Polygon))
    meshes = []
    for piece in quantized_pieces:
        if piece.area < 1e-8:
            continue
        # Keep every boundary segment, including nearly collinear foundation
        # samples and shared ridge edges. Ear clipping can leave zero-area
        # bridges here; removing them disconnects small roof triangles and
        # causes the extruded walls to meet along nonmanifold edges.
        corners = np.asarray([
            np.asarray(triangle.exterior.coords)[:3]
            for triangle in constrained_delaunay_triangles(piece).geoms
        ])
        xy, inverse = np.unique(corners.reshape(-1, 2), axis=0, return_inverse=True)
        triangles = inverse.reshape(-1, 3)
        uv = (xy - center) @ np.column_stack([axis, cross])
        if shape == "flat":
            rise = np.zeros(len(xy))
        else:
            rise = (half_width - np.abs(uv[:, 1])) / half_width
            if shape == "hipped":
                rise = np.minimum(rise, (half_length - np.abs(uv[:, 0])) / half_width)
            rise = np.clip(rise, 0, 1) * roof_height
        vertices = np.column_stack([xy, base_z + rise]).astype(np.float32).astype(np.float64)
        # Triangulation orientation can vary with the input ring. Normals face up.
        triangles = np.asarray(triangles).copy()
        cross_z = np.cross(vertices[triangles[:, 1]] - vertices[triangles[:, 0]], vertices[triangles[:, 2]] - vertices[triangles[:, 0]])[:, 2]
        triangles[cross_z < 0] = triangles[cross_z < 0, ::-1]
        meshes.append(trimesh.Trimesh(vertices=vertices, faces=triangles, process=False))
    roof = trimesh.util.concatenate(meshes)
    roof.merge_vertices()
    # Overlaying ridge lines can create round-off slivers along a split edge.
    # Remove collapsed faces before finding boundary edges, otherwise they can
    # hide an exterior edge or create an extra wall through the building.
    triangle_xy = roof.triangles[:, :, :2]
    edge_a = triangle_xy[:, 1] - triangle_xy[:, 0]
    edge_b = triangle_xy[:, 2] - triangle_xy[:, 0]
    projected_twice_area = edge_a[:, 0] * edge_b[:, 1] - edge_a[:, 1] * edge_b[:, 0]
    roof.update_faces(projected_twice_area > 2e-10)
    roof.update_faces(roof.unique_faces())
    roof.remove_unreferenced_vertices()
    return roof


def _mesh_for_polygon(polygon, tags, elevation, config):
    total_height = building_height(tags, config)
    shape = str(tags.get("roof:shape", "gabled" if tags.get("building") in PITCHED_TYPES else "flat")).lower()
    if shape not in ("flat", "gabled", "hipped"):
        shape = "flat"
    # Add boundary points so foundations follow slopes along long building edges.
    polygon = polygon.segmentize(float(config.get("foundation_sample_spacing_m", 8.0)))
    boundary = np.vstack([np.asarray(ring.coords) for ring in [polygon.exterior, *polygon.interiors]])
    ground = np.asarray(elevation(boundary[:, 0], boundary[:, 1]), dtype=float)
    if not np.all(np.isfinite(ground)):
        raise ValueError("terrain sampling returned non-finite building elevations")
    foundation_z = float(np.max(ground))
    width = float(min(_axes(polygon)[3]) * 2)
    roof_height = 0.0
    if shape != "flat":
        roof_height = parse_length(tags.get("roof:height")) or min(3.0, width * 0.25, total_height * 0.3)
        roof_height = min(roof_height, max(0.0, total_height - 0.5))
    roof_base = foundation_z + total_height - roof_height
    roof = _roof_surface(polygon, shape, roof_base, roof_height)
    count = len(roof.vertices)
    lower = roof.vertices.copy()
    lower[:, 2] = np.asarray(elevation(lower[:, 0], lower[:, 1]), dtype=float) - 0.08
    vertices = np.vstack([roof.vertices, lower])
    # The upward-oriented top boundary gives consistent outward wall normals on
    # both outer rings and courtyard rings. Include a bottom cap below terrain.
    edges = roof.edges_sorted
    _, inverse, counts = np.unique(edges, axis=0, return_inverse=True, return_counts=True)
    boundary_edges = roof.edges[counts[inverse] == 1]
    walls = []
    for a, b in boundary_edges:
        walls.extend([[a, a + count, b + count], [a, b + count, b]])
    faces = np.vstack([np.asarray(walls, dtype=np.int64), roof.faces[:, ::-1] + count])
    wall = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    wall.remove_unreferenced_vertices()
    wall.visual = trimesh.visual.TextureVisuals(material=WALL_MATERIAL)
    roof.visual = trimesh.visual.TextureVisuals(material=ROOF_MATERIAL)
    return wall, roof, shape


def add_buildings(scene: trimesh.Scene, features: list, elevation, config: dict) -> dict:
    """Append buildings to a scene in local east/north/up meters and return counts."""
    options = config.get("buildings", config)
    stats = {"buildings": 0, "building_meshes": 0, "skipped_buildings": 0}
    roof_shapes = Counter()
    for feature in features:
        geometry = feature["geometry"]
        polygons = list(geometry.geoms) if isinstance(geometry, MultiPolygon) else [geometry]
        added = False
        for index, polygon in enumerate(polygons):
            if not isinstance(polygon, Polygon) or polygon.area < options.get("minimum_area_m2", 2.0):
                continue
            try:
                wall, roof, shape = _mesh_for_polygon(polygon, feature.get("tags", {}), elevation, options)
            except (ValueError, IndexError, TypeError, ShapelyError) as error:
                LOG.warning("Skipping building %s component %d: %s", feature["id"], index, error)
                continue
            name = f"building_{feature['id'].replace('/', '_')}_{index}"
            scene.add_geometry(wall, node_name=f"{name}_walls", geom_name=f"{name}_walls")
            scene.add_geometry(roof, node_name=f"{name}_roof", geom_name=f"{name}_roof")
            stats["building_meshes"] += 2
            roof_shapes[shape] += 1
            added = True
        stats["buildings" if added else "skipped_buildings"] += 1
    stats["roof_shapes"] = dict(roof_shapes)
    LOG.info("Generated %d buildings (%s)", stats["buildings"], stats["roof_shapes"])
    return stats
