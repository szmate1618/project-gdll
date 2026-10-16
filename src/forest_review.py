"""Build runtime forest patches from independent, reviewed local canopy traces.

Coordinates are local east/north metres. Exclusions happen before 500 m grid
clipping, so grid boundaries never change the interpreted woodland boundary.
"""
from __future__ import annotations

import math

from shapely import set_precision
from shapely.geometry import LineString, Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union


def _polygons(geometry):
    if isinstance(geometry, Polygon):
        return [geometry] if not geometry.is_empty else []
    return [p for g in getattr(geometry, "geoms", ()) for p in _polygons(g)]


def _ring(ring):
    return [[round(x, 3), round(y, 3)] for x, y in ring.coords]


def _polygon(points, name):
    polygon = Polygon(points)
    if not polygon.is_valid or polygon.is_empty or polygon.area <= 0:
        raise ValueError(f"Invalid forest review polygon: {name}")
    return polygon


def build_patches(trace):
    """Return region summaries and validated patches; never repair bad traces."""
    if trace.get("schema") != "godollo.astra_satellite_forest_traces" or trace.get("version") != 1:
        raise ValueError("Unsupported satellite forest trace schema/version")
    parameters = trace["parameters"]
    cell_size = parameters["grid_cell_size_m"]
    if cell_size != 500 or parameters["coordinate_precision_m"] != .001:
        raise ValueError("Runtime forest subdivision requires 500 m cells and 0.001 m precision")
    for name, value in parameters.items():
        if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            raise ValueError(f"Invalid forest review parameter: {name}")
    bounds = box(*trace["coordinate_frame"]["bounds_m"]).buffer(-parameters["terrain_edge_inset_m"])
    if bounds.is_empty:
        raise ValueError("Forest terrain bounds are empty")
    patches, summaries, geometries, identifiers = [], [], [], set()
    for record in trace["regions"]:
        identifier = record["region_id"]
        if not identifier or identifier in identifiers:
            raise ValueError("Duplicate or empty forest review region ID")
        identifiers.add(identifier)
        shape = _polygon(record["polygon"], identifier).buffer(
            -parameters["canopy_edge_inset_m"], join_style="mitre")
        for clearing in record["excluded_clearings"]:
            shape = shape.difference(_polygon(clearing, identifier).buffer(
                parameters["clearing_margin_m"], join_style="mitre"))
        for track in record["excluded_track_centrelines"]:
            line = LineString(track)
            if not line.is_valid or line.length <= 0:
                raise ValueError(f"Invalid forest track in {identifier}")
            shape = shape.difference(line.buffer(parameters["track_half_width_m"],
                                                 cap_style="flat", join_style="mitre"))
        shape = shape.intersection(bounds)
        shape = unary_union([p for p in _polygons(shape)
                             if p.area >= parameters["minimum_component_area_m2"]])
        shape = set_precision(shape, .001)
        if not shape.is_valid or shape.is_empty:
            raise ValueError(f"Review region has no valid retained geometry: {identifier}")
        start = len(patches)
        minx, miny, maxx, maxy = shape.bounds
        for gx in range(math.floor(minx / cell_size), math.floor(maxx / cell_size) + 1):
            for gy in range(math.floor(miny / cell_size), math.floor(maxy / cell_size) + 1):
                cell = box(gx * cell_size, gy * cell_size, (gx + 1) * cell_size, (gy + 1) * cell_size)
                pieces = _polygons(shape.intersection(cell))
                pieces.sort(key=lambda p: (p.bounds, p.area))
                for i, piece in enumerate(pieces):
                    if piece.area < parameters["minimum_patch_area_m2"]:
                        continue
                    piece = orient(set_precision(piece, .001), sign=1)
                    rings = [piece.exterior, *piece.interiors]
                    if (not piece.is_valid or not piece.exterior.is_ccw
                            or any(h.is_ccw for h in piece.interiors)
                            or any(not 4 <= len(r.coords) <= 1024 for r in rings)
                            or len(piece.interiors) > 64):
                        raise ValueError(f"Invalid runtime forest polygon: {identifier}")
                    if max(piece.bounds[2] - piece.bounds[0], piece.bounds[3] - piece.bounds[1]) > 500.001:
                        raise ValueError("Forest patch exceeds runtime bounds")
                    patches.append(dict(id=f"{identifier}_{gx}_{gy}_{i}", region_id=identifier,
                                        grid_cell=[gx, gy], bounds_m=list(piece.bounds),
                                        area_m2=round(piece.area, 3), polygon=_ring(piece.exterior),
                                        holes=[_ring(h) for h in piece.interiors]))
                    geometries.append(piece)
        if start == len(patches):
            raise ValueError(f"Review region has no retained grid fragments: {identifier}")
        summaries.append(dict(id=identifier, label=record["label"], kind=record["kind"],
                              confidence=record["confidence"], patch_count=len(patches) - start,
                              area_m2=round(sum(p["area_m2"] for p in patches[start:]), 3),
                              review_policy="Independent satellite visual tracing; obvious fields, "
                              "built-up areas, clearings and selected visible tracks excluded."))
    if abs(unary_union(geometries).area - sum(p.area for p in geometries)) > .01:
        raise ValueError("Forest review regions overlap")
    return summaries, patches
