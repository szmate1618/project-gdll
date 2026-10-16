"""Convert Overpass ``out geom`` elements to clipped local metric features."""

from __future__ import annotations

import logging
import math

from shapely import make_valid
from shapely.errors import ShapelyError
from shapely.geometry import LineString, MultiPolygon, Point, Polygon
from shapely.ops import polygonize, unary_union

LOG = logging.getLogger(__name__)
RAILWAY_TYPES = frozenset({"rail", "light_rail", "narrow_gauge"})
FENCE_ENTRANCES = frozenset({"gate", "entrance", "lift_gate", "swing_gate", "kissing_gate", "stile"})


def _polygonal(geometry):
    """Discard non-area remnants of clipping/repair without losing valid islands."""
    if isinstance(geometry, Polygon):
        return geometry if geometry.area > 0.01 else None
    polygons = []
    for part in getattr(geometry, "geoms", []):
        candidate = _polygonal(part)
        if isinstance(candidate, Polygon):
            polygons.append(candidate)
        elif isinstance(candidate, MultiPolygon):
            polygons.extend(candidate.geoms)
    return unary_union(polygons) if polygons else None


def _geometry_reader(payload: dict, transformer, origin: tuple[float, float]):
    """Index OSM sources and project inline geometry or referenced node points."""
    elements = payload.get("elements", [])
    ways = {element["id"]: element for element in elements if element.get("type") == "way"}
    nodes = {
        element["id"]: element
        for element in elements
        if element.get("type") == "node" and "lon" in element and "lat" in element
    }

    def coordinates(element):
        points = element.get("geometry") or [nodes.get(node, {}) for node in element.get("nodes", [])]
        if len(points) < 2 or any("lon" not in point or "lat" not in point for point in points):
            return []
        xx, yy = transformer.transform([point["lon"] for point in points], [point["lat"] for point in points])
        return [(x - origin[0], y - origin[1]) for x, y in zip(xx, yy)]

    return elements, ways, coordinates


def _clipped_line_features(way: dict, coords: list, clip: Polygon) -> list:
    geometry = LineString(coords).intersection(clip)
    parts = [geometry] if isinstance(geometry, LineString) else getattr(geometry, "geoms", [])
    lines = [part for part in parts if isinstance(part, LineString) and part.length > 0.05]
    return [
        {"id": f"way/{way['id']}" + (f":{index}" if len(lines) > 1 else ""),
         "geometry": line, "tags": dict(way.get("tags", {}))}
        for index, line in enumerate(lines)
    ]


def _relation_area(outer: list, inner: list):
    """Stitch rings and attach each hole to its smallest containing shell."""
    # Polygonize adds holes for nested rings even when both have role outer.
    shells = sorted((Polygon(part.exterior) for part in polygonize(unary_union(outer))),
                    key=lambda shell: shell.area)
    holes = [[] for _ in shells]
    for part in polygonize(unary_union(inner)):
        hole = Polygon(part.exterior)
        for index, shell in enumerate(shells):
            if shell.covers(hole):
                holes[index].append(hole)
                break
    # Complete each component before unioning, so courtyards retain outer islands.
    return unary_union([
        shell.difference(unary_union(component_holes)) if component_holes else shell
        for shell, component_holes in zip(shells, holes)
    ])


def parse_osm(payload: dict, transformer, origin: tuple[float, float], clip: Polygon):
    """Return ``(buildings, roads)`` with projected, origin-relative geometries.

    Each feature has ``id`` (``way/123`` or ``relation/123``), ``geometry``, and
    ``tags``. Relation member lines are stitched before constructing courtyard
    holes. Successfully assembled relation members are omitted as standalone
    buildings, but remain eligible as roads. Railways are read separately by
    :func:`parse_railways` to preserve this return contract.
    """
    elements, ways, coordinates = _geometry_reader(payload, transformer, origin)

    def clipped_area(geometry):
        return _polygonal(make_valid(geometry).intersection(clip))

    buildings, roads, consumed = [], [], set()
    for relation in elements:
        tags = relation.get("tags", {})
        if relation.get("type") != "relation" or tags.get("building", "no") == "no":
            continue
        outer, inner, members = [], [], []
        inherited_tags = {}
        for member in relation.get("members", []):
            if member.get("type") != "way":
                continue
            source = member if member.get("geometry") else ways.get(member.get("ref"), member)
            coords = coordinates(source)
            role = member.get("role", "")
            if len(coords) < 2 or role not in ("", "outer", "inner"):
                continue
            (inner if role == "inner" else outer).append(LineString(coords))
            members.append(member["ref"])
            if role != "inner":
                inherited_tags.update(ways.get(member["ref"], {}).get("tags", {}))
        if not outer:
            continue
        try:
            geometry = clipped_area(_relation_area(outer, inner))
        except (ValueError, TypeError, ShapelyError) as error:
            LOG.warning("Skipping malformed building relation %s: %s", relation.get("id"), error)
            continue
        if geometry is None or geometry.is_empty:
            continue
        inherited_tags.update(tags)
        buildings.append({"id": f"relation/{relation['id']}", "geometry": geometry, "tags": inherited_tags})
        consumed.update(members)

    for way in ways.values():
        tags = way.get("tags", {})
        if not tags:
            continue
        coords = coordinates(way)
        if len(coords) < 2:
            continue
        if tags.get("building", "no") != "no" and way["id"] not in consumed and len(coords) >= 4:
            # OSM building ways must be closed. Never invent a closing edge for
            # an incomplete extract, which could produce a huge false building.
            if coords[0] == coords[-1]:
                geometry = clipped_area(Polygon(coords))
                if geometry is not None and not geometry.is_empty:
                    buildings.append({"id": f"way/{way['id']}", "geometry": geometry, "tags": dict(tags)})
        if tags.get("highway", "no") != "no":
            roads.extend(_clipped_line_features(way, coords, clip))
    LOG.info("Parsed %d building footprints and %d road centerlines", len(buildings), len(roads))
    return buildings, roads


def parse_railways(payload: dict, transformer, origin: tuple[float, float], clip: Polygon) -> list:
    """Read active railway track centerlines in local east/north metres.

    Include conventional, light and narrow-gauge tracks regardless of operator
    tags, so HÉV and MÁV paths are retained even when operator metadata is absent.
    Route relations do not add geometry: their track ways are already included,
    once per OSM way. Lifecycle and non-track values are excluded.
    """
    _, ways, coordinates = _geometry_reader(payload, transformer, origin)
    railways = []
    for way in ways.values():
        if way.get("tags", {}).get("railway") not in RAILWAY_TYPES:
            continue
        coords = coordinates(way)
        if len(coords) >= 2:
            railways.extend(_clipped_line_features(way, coords, clip))
    LOG.info("Parsed %d railway centerlines", len(railways))
    return railways


def parse_guardrails(payload: dict, transformer, origin: tuple[float, float], clip: Polygon) -> list:
    """Read only explicitly mapped roadside guardrail lines, retaining gaps."""
    _, ways, coordinates = _geometry_reader(payload, transformer, origin)
    features = []
    for way in ways.values():
        if way.get("tags", {}).get("barrier") != "guard_rail":
            continue
        coords = coordinates(way)
        if len(coords) >= 2:
            if not all(math.isfinite(value) for point in coords for value in point):
                raise ValueError(f"Guardrail way/{way['id']} has nonfinite coordinates")
            features.extend(_clipped_line_features(way, coords, clip))
    return features


def parse_fences(payload: dict, transformer, origin: tuple[float, float], clip: Polygon) -> list:
    """Read mapped fence ways and their entrance nodes in local east/north metres.

    Keep entrance positions only when their node belongs to the fence way;
    nearby gates on unrelated ways must not cut a hole in this fence. Retain
    the unclipped line until geometry generation so openings at clip boundaries
    and at the seam of a closed fence can be cut before clipping.
    """
    elements, ways, coordinates = _geometry_reader(payload, transformer, origin)
    entrances = {element["id"]: element for element in elements
                 if element.get("type") == "node"
                 and element.get("tags", {}).get("barrier") in FENCE_ENTRANCES}
    fences = []
    for way in ways.values():
        if way.get("tags", {}).get("barrier") != "fence":
            continue
        coords = coordinates(way)
        if len(coords) < 2:
            continue
        line = LineString(coords)
        if line.length <= .05 or not line.intersects(clip):
            continue
        openings = []
        for node_id in dict.fromkeys(way.get("nodes", [])):
            node = entrances.get(node_id)
            if node is None or "lon" not in node or "lat" not in node:
                continue
            x, y = transformer.transform(node["lon"], node["lat"])
            point = Point(x - origin[0], y - origin[1])
            if line.distance(point) < 1e-5:
                openings.append({"id": f"node/{node_id}", "geometry": point,
                                 "tags": dict(node["tags"])})
        fences.append({"id": f"way/{way['id']}", "geometry": line,
                       "tags": dict(way["tags"]), "openings": openings})
    LOG.info("Parsed %d fence ways", len(fences))
    return fences
