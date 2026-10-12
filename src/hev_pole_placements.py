"""Approximate a single catenary mast row between mapped HÉV main tracks."""
from __future__ import annotations

import json
import math
from pathlib import Path

from shapely.geometry import LineString
from shapely.ops import linemerge, unary_union
from shapely.strtree import STRtree

from .osm import parse_railways
from .tree_map import _document

HEV_POLE_SPACING_M = 50.0
HEV_POLE_HEIGHT_M = 8.0
HEV_POLE_WIDTH_M = .30


def map_railways(map_path, frame, root):
    """Reuse the town's recorded OSM extract, including after moving a checkout."""
    with Path(map_path).open("rb") as stream:
        document, _ = _document(stream)
    source = document["scenes"][document.get("scene", 0)].get("extras", {}).get("sources", {}).get("osm")
    if not source:
        return [], {}  # A terrain-only map has no railway provenance.
    recorded = Path(source["path"])
    cached = Path(root) / "data/raw/osm" / recorded.name
    path = cached if cached.is_file() else recorded
    payload = json.loads(path.read_text(encoding="utf-8"))
    timestamp = source.get("timestamp_osm_base")
    if timestamp and payload.get("osm3s", {}).get("timestamp_osm_base") != timestamp:
        raise ValueError("Cached railway extract differs from the town's recorded OSM snapshot")
    return parse_railways(payload, frame["transformer"], frame["origin"], frame["clip"]), source


def _tangent(line, distance):
    a = line.interpolate(max(0, distance - 1))
    b = line.interpolate(min(line.length, distance + 1))
    length = a.distance(b)
    return ((b.x - a.x) / length, (b.y - a.y) / length) if length > .001 else (0, 0)


def make_hev_columns(features, terrain, spacing=HEV_POLE_SPACING_M):
    """Sample paired track midpoints in east/north; export bases as X/Y/-north.

    Join OSM way seams before sampling. Only HÉV main tracks participate: yard
    tracks, crossovers and the adjacent MÁV railway must not attract the row.
    A 2.5–12 m separation and parallel tangents reject switches and single track.
    The lower-index track owns each pair, so both tracks produce just one row.
    Dimensions and spacing are visual approximations, not infrastructure surveys.
    """
    if not math.isfinite(spacing) or spacing <= 0:
        raise ValueError("HÉV pole spacing must be positive and finite")
    selected = [f["geometry"] for f in features
                if f["tags"].get("railway") == "light_rail"
                and "hév" in f["tags"].get("operator", "").casefold()
                and not f["tags"].get("service")
                and f["tags"].get("electrified") != "no"]
    if not selected:
        return []
    union = unary_union(selected)
    merged = union if isinstance(union, LineString) else linemerge(union)
    lines = [merged] if isinstance(merged, LineString) else list(merged.geoms)
    # Canonical direction/order makes placements independent of OSM way order.
    canonical = []
    for line in lines:
        if line.length <= .01:
            continue
        points = list(line.coords)
        if points[0] > points[-1]:
            points.reverse()
        canonical.append(LineString(points))
    lines = canonical
    lines.sort(key=lambda line: tuple(line.coords))
    index = STRtree(lines)
    columns, occupied = [], {}
    minimum_gap = spacing * .75

    def publish_run(points):
        if len(points) < 2:
            return
        centerline = LineString(points)
        for ordinal in range(max(0, math.ceil((centerline.length - spacing / 2) / spacing))):
            point = centerline.interpolate(spacing / 2 + ordinal * spacing)
            cell = (math.floor(point.x / minimum_gap), math.floor(point.y / minimum_gap))
            neighbors = [p for dx in (-1, 0, 1) for dy in (-1, 0, 1)
                         for p in occupied.get((cell[0] + dx, cell[1] + dy), [])]
            if any(point.distance(p) < minimum_gap for p in neighbors):
                continue
            height = float(terrain(point.x, point.y))
            if not all(math.isfinite(v) for v in (point.x, point.y, height)):
                raise ValueError("HÉV pole cannot be placed on map terrain")
            occupied.setdefault(cell, []).append(point)
            columns.append({"id": f"hev/{len(columns)}", "kind": "hev_catenary",
                            "position": [point.x, height, -point.y],
                            "height_m": HEV_POLE_HEIGHT_M, "width_m": HEV_POLE_WIDTH_M})

    for owner, line in enumerate(lines):
        run = []
        steps = max(1, math.ceil(line.length / 5))
        for step in range(steps + 1):
            distance = line.length * step / steps
            point = line.interpolate(distance)
            tangent = _tangent(line, distance)
            partners = []
            for other in index.query(point.buffer(12)):
                if other <= owner:
                    continue
                peer = lines[other]
                peer_distance = peer.project(point)
                nearest = peer.interpolate(peer_distance)
                gap = point.distance(nearest)
                direction = _tangent(peer, peer_distance)
                along = abs((nearest.x - point.x) * tangent[0] + (nearest.y - point.y) * tangent[1])
                if (2.5 <= gap <= 12 and along <= max(.5, gap * .25)
                        and abs(tangent[0] * direction[0] + tangent[1] * direction[1]) >= .95):
                    partners.append((gap, nearest))
            if not partners:
                publish_run(run)
                run = []
                continue
            _, peer = min(partners, key=lambda pair: pair[0])
            middle = ((point.x + peer.x) / 2, (point.y + peer.y) / 2)
            if run and math.dist(run[-1], middle) > 10:
                publish_run(run)
                run = []
            run.append(middle)
        publish_run(run)
    return columns
