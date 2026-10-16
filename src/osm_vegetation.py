"""Read OSM tree points, tree rows and woodland polygons in local east/north metres."""
from __future__ import annotations

import math

from shapely import make_valid
from shapely.geometry import LineString, Point, Polygon
from shapely.ops import polygonize_full, unary_union

from .osm import _clipped_line_features, _geometry_reader, _polygonal, _relation_area


def vegetation_query(bbox):
    west, south, east, north = bbox
    bounds = f"{south},{west},{north},{east}"
    return ('[out:json][timeout:90];\n(\n'
            f'  node["natural"="tree"]({bounds});\n'
            f'  way["natural"="tree_row"]({bounds});\n'
            f'  way["natural"="wood"]({bounds});\n'
            f'  way["landuse"="forest"]({bounds});\n'
            f'  relation["type"="multipolygon"]["natural"="wood"]({bounds});\n'
            f'  relation["type"="multipolygon"]["landuse"="forest"]({bounds});\n'
            ');\nout body geom;\n')


def _woodland(tags):
    return tags.get("natural") == "wood" or tags.get("landuse") == "forest"


def _check_lines(lines, label):
    if not lines:
        return []
    polygons, cuts, dangles, invalid = polygonize_full(unary_union(lines))
    if not all(part.is_empty for part in (cuts, dangles, invalid)):
        raise ValueError(f"Incomplete woodland {label}; refusing to invent a boundary or lose a clearing")
    return [Polygon(part.exterior) for part in polygons.geoms]


def parse_vegetation(payload, frame):
    """Return trees, rows and forests; reject incomplete forest rings.

    Accept Overpass inline geometry or OSM API node/way references. Relation
    member ways are consumed once; courtyards/clearings and outer islands remain.
    """
    elements, ways, coordinates = _geometry_reader(payload, frame["transformer"], frame["origin"])
    clip = frame["clip"]
    trees, rows, forests, consumed = [], [], [], set()
    seen = set()

    def feature(element, geometry):
        return {"id": f"{element['type']}/{element['id']}",
                "geometry": geometry, "tags": dict(element.get("tags", {}))}

    def area(geometry):
        if not all(math.isfinite(v) for v in geometry.bounds):
            raise ValueError("Woodland coordinates cannot be projected into the map frame")
        return _polygonal(make_valid(geometry).intersection(clip))

    for element in elements:
        key = element["type"], element["id"]
        if key in seen:
            continue
        seen.add(key)
        tags = element.get("tags", {})
        if element["type"] == "node" and tags.get("natural") == "tree":
            lon, lat = element["lon"], element["lat"]
            if not all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)
                       for v in (lon, lat)):
                raise ValueError(f"Tree node/{element['id']} has invalid coordinates")
            x, n = frame["transformer"].transform(lon, lat)
            point = Point(x - frame["origin"][0], n - frame["origin"][1])
            if not math.isfinite(point.x) or not math.isfinite(point.y):
                raise ValueError(f"Tree node/{element['id']} cannot be projected")
            if clip.covers(point):
                trees.append(feature(element, point))
        if element["type"] != "relation" or not _woodland(tags) or tags.get("type") != "multipolygon":
            continue
        outer, inner, members = [], [], []
        for member in element.get("members", []):
            if member.get("type") != "way" or member.get("role", "") not in ("", "outer", "inner"):
                continue
            source = member if member.get("geometry") else ways.get(member["ref"], member)
            coords = coordinates(source)
            if len(coords) < 2:
                raise ValueError(f"Incomplete woodland relation/{element['id']}: missing way/{member['ref']}")
            (inner if member.get("role") == "inner" else outer).append(LineString(coords))
            members.append(member["ref"])
        if not outer:
            raise ValueError(f"Woodland relation/{element['id']} has no outer boundary")
        shells = _check_lines(outer, f"relation/{element['id']} outer")
        holes = _check_lines(inner, f"relation/{element['id']} inner")
        if any(not any(shell.covers(hole) for shell in shells) for hole in holes):
            raise ValueError(f"Woodland relation/{element['id']} has a clearing outside its outer boundary")
        geometry = area(_relation_area(outer, inner))
        consumed.update(members)
        if geometry is not None and not geometry.is_empty:
            forests.append(feature(element, geometry))

    for way in ways.values():
        tags = way.get("tags", {})
        is_forest = _woodland(tags) and way["id"] not in consumed
        if not is_forest and tags.get("natural") != "tree_row":
            continue
        coords = coordinates(way)
        if len(coords) < 2:
            raise ValueError(f"Incomplete vegetation way/{way['id']}")
        if is_forest:
            if len(coords) < 4 or coords[0] != coords[-1]:
                raise ValueError(f"Woodland way/{way['id']} is not closed")
            geometry = area(Polygon(coords))
            if geometry is not None and not geometry.is_empty:
                forests.append(feature(way, geometry))
        if tags.get("natural") == "tree_row":
            rows.extend(_clipped_line_features(way, coords, clip))
    return tuple(sorted(items, key=lambda item: item["id"]) for items in (trees, rows, forests))
