"""Convert Overpass ``out geom`` elements to clipped local metric features."""

from __future__ import annotations

import logging

from shapely import make_valid
from shapely.errors import ShapelyError
from shapely.geometry import LineString, MultiPolygon, Polygon
from shapely.ops import polygonize, unary_union

LOG = logging.getLogger(__name__)


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


def parse_osm(payload: dict, transformer, origin: tuple[float, float], clip: Polygon):
    """Return ``(buildings, roads)`` with projected, origin-relative geometries.

    Each feature has ``id`` (``way/123`` or ``relation/123``), ``geometry``, and
    ``tags``. Relation member lines are stitched before constructing courtyard
    holes. Successfully assembled relation members are omitted as standalone
    buildings, but remain eligible as roads.
    """
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
            shell = unary_union(list(polygonize(unary_union(outer))))
            if inner:
                shell = shell.difference(unary_union(list(polygonize(unary_union(inner)))))
            geometry = clipped_area(shell)
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
            geometry = LineString(coords).intersection(clip)
            parts = [geometry] if isinstance(geometry, LineString) else getattr(geometry, "geoms", [])
            lines = [part for part in parts if isinstance(part, LineString) and part.length > 0.05]
            for index, line in enumerate(lines):
                suffix = f":{index}" if len(lines) > 1 else ""
                roads.append({"id": f"way/{way['id']}{suffix}", "geometry": line, "tags": dict(tags)})
    LOG.info("Parsed %d building footprints and %d road centerlines", len(buildings), len(roads))
    return buildings, roads
