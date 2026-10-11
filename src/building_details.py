"""Road-facing entrances and window openings in local east/north/up meters."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from shapely.geometry import LineString, Polygon
from shapely.geometry.polygon import orient
from shapely.ops import nearest_points
from shapely.strtree import STRtree
from trimesh.visual.material import PBRMaterial

from .wall_openings import cut_wall_openings

DOOR_MATERIAL = PBRMaterial(name="building_door_brown", baseColorFactor=[112, 65, 32, 255],
                            doubleSided=True, metallicFactor=0.0, roughnessFactor=0.9)
# glTF alpha is opacity: 40% opaque means 60% transparent.
WINDOW_MATERIAL = PBRMaterial(name="building_window_blue", baseColorFactor=[86, 156, 210, 102],
                              alphaMode="BLEND", doubleSided=True, metallicFactor=0.0, roughnessFactor=0.25)


@dataclass
class Facade:
    start: np.ndarray
    end: np.ndarray
    direction: np.ndarray
    normal: np.ndarray
    length: float
    exterior: bool

    @property
    def line(self):
        return LineString([self.start, self.end])


class RoadIndex:
    """Build once per map, then find the closest road to each footprint."""

    def __init__(self, features):
        self.lines = [feature["geometry"] for feature in features
                      if not feature["geometry"].is_empty]
        self.tree = STRtree(self.lines) if self.lines else None

    def nearest(self, polygon):
        return self.lines[int(self.tree.nearest(polygon))] if self.tree is not None else None


def _facades(polygon):
    # Merge collinear OSM vertices before laying out windows. Foundation sampling
    # must not create extra facades. CCW shells and CW holes put outward normals
    # on the right, including normals pointing into courtyards.
    polygon = orient(polygon.simplify(1e-5, preserve_topology=True), sign=1.0)
    result = []
    for ring_index, ring in enumerate([polygon.exterior, *polygon.interiors]):
        points = np.asarray(ring.coords)
        for start, end in zip(points[:-1], points[1:]):
            length = float(np.linalg.norm(end - start))
            if length == 0:
                continue
            direction = (end - start) / length
            result.append(Facade(start, end, direction, np.array([direction[1], -direction[0]]),
                                 length, ring_index == 0))
    return result


def _door_facades(facades, road):
    candidates = [index for index, facade in enumerate(facades)
                  if facade.exterior and facade.length >= 1.6]
    if not candidates:
        return []
    if road is None:
        front = max(candidates, key=lambda index: facades[index].length)
    else:
        def facing_score(index):
            facade = facades[index]
            # A corner can be equally close for two walls. Use the wall center
            # to measure facing, rather than an ambiguous corner-to-road vector.
            center = facade.line.interpolate(.5, normalized=True)
            _, road_point = nearest_points(center, road)
            towards_road = np.asarray(road_point.coords[0]) - np.asarray(center.coords[0])
            length = np.linalg.norm(towards_road)
            return float(np.dot(facade.normal, towards_road) / length) if length > 1e-6 else 0.0

        distances = {index: facades[index].line.distance(road) for index in candidates}
        closest = min(distances.values())
        # Treat walls within half a meter as similarly close, then prefer the
        # normal pointing most directly toward the road. Distance breaks ties.
        near = [index for index in candidates if distances[index] <= closest + .5]
        facing = [index for index in near if facing_score(index) > 0]
        front = min(facing, key=lambda index: (-facing_score(index), distances[index])) if facing else min(candidates, key=distances.get)
    # Concave outlines may have several parallel rear walls. Prefer the most
    # opposite normal, then the wall furthest behind the front facade.
    front_facade = facades[front]
    opposite = [index for index in candidates if
                np.dot(facades[index].normal, front_facade.normal) < -0.5]
    if not opposite:
        return [front]
    rear = min(opposite, key=lambda index: (
        round(float(np.dot(facades[index].normal, front_facade.normal)), 6),
        float(np.dot((facades[index].start + facades[index].end) * 0.5 - front_facade.start,
                     front_facade.normal))))
    return [front, rear]


def _rectangle(facade, center, width, bottom, height):
    left = facade.start + facade.direction * (center - width * 0.5)
    right = left + facade.direction * width
    # Direction cross up points out of the solid, including courtyard facades.
    return np.array([[*left, bottom], [*right, bottom],
                     [*right, bottom + height], [*left, bottom + height]])


def _foundation_profile(wall, facade, left, right):
    """Sample the existing piecewise-linear wall base across an opening."""
    xy, inverse = np.unique(wall.vertices[:, :2], axis=0, return_inverse=True)
    heights = np.full(len(xy), np.inf)
    np.minimum.at(heights, inverse, wall.vertices[:, 2])
    delta = xy - facade.start
    along = delta @ facade.direction
    on_wall = ((np.abs(delta @ facade.normal) < .002) &
               (along >= -.002) & (along <= facade.length + .002))
    order = np.argsort(along[on_wall])
    coordinates, heights = along[on_wall][order], heights[on_wall][order]
    if len(coordinates) < 2:
        raise ValueError("Cannot sample building foundation across a door")
    samples = np.r_[left, coordinates[(coordinates > left) & (coordinates < right)], right]
    return np.interp(samples, coordinates, heights)


def facade_details(polygon: Polygon, wall, roof, elevation, options, road=None):
    """Return walls with openings, door/glass infills, and rectangle counts.

    The lowest roof vertex is the conservative eave height. Rectangles fit below
    it, so gables and hip slopes keep the same closed roof/wall geometry.
    """
    facades = _facades(polygon)
    eaves = float(roof.vertices[:, 2].min())
    boundary = polygon.segmentize(float(options.get("foundation_sample_spacing_m", 8.0)))
    samples = np.vstack([np.asarray(ring.coords) for ring in [boundary.exterior, *boundary.interiors]])
    foundation = float(np.max(elevation(samples[:, 0], samples[:, 1])))
    # One exported-precision head height aligns both doors and the lowest
    # window row. Door panels extend down to each facade's sampled wall base.
    first_head = float(np.float32(foundation + 2.1))
    doors, windows = [], []
    openings = {}
    door_spans = {}
    for index in _door_facades(facades, road):
        facade = facades[index]
        center = facade.length * 0.5
        if first_head > eaves - 0.15:
            continue
        # The cutter reaches below the entire base profile. Intersection with
        # the wall outline gives the infill its exact sloping bottom boundary.
        bottom = float(np.min(_foundation_profile(wall, facade, center - .5, center + .5))) - .01
        doors.append(_rectangle(facade, center, 1.0, bottom, first_head - bottom))
        openings.setdefault(index, []).append(("doors", doors[-1]))
        door_spans[index] = (center - 0.65, center + 0.65, bottom, first_head + .15)

    floor_height = float(options.get("floor_height_m", 3.0))
    for index, facade in enumerate(facades):
        if facade.length < 1.8:
            continue
        columns = max(1, int(facade.length / 3.0))
        for bottom in np.arange(first_head - 1.2, eaves - 1.5, floor_height):
            for column in range(columns):
                center = facade.length * (column + 0.5) / columns
                span = door_spans.get(index)
                if span and center + 0.6 > span[0] and center - 0.6 < span[1] and bottom < span[3] and bottom + 1.2 > span[2]:
                    continue
                windows.append(_rectangle(facade, center, 1.2, float(bottom), 1.2))
                openings.setdefault(index, []).append(("windows", windows[-1]))
    if not openings:
        return None, None, None, {"doors": 0, "windows": 0}
    detailed_wall, door_mesh, window_mesh = cut_wall_openings(wall, facades, openings)
    return detailed_wall, door_mesh, window_mesh, {"doors": len(doors), "windows": len(windows)}
