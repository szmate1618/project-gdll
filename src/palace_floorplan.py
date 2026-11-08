"""Evidence-based Gödöllő palace layout and facade openings, in plan metres.

The small authored input is independent of the configured town frame. Its X axis
runs along the street facade; Y runs from the entrance towards the gardens.
Only this mapped palace is overridden. Runtime receives the prepared layout in
the wall mesh extras; it needs neither the source photos nor Python/JSON files.
"""
from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path

import numpy as np
from shapely.geometry import Point, Polygon, box
from shapely.ops import nearest_points
import trimesh

from .building_details import _facades, _rectangle
from .building_entrances import ENTRANCE_MATERIAL
from .config import coordinate_frame
from .wall_openings import cut_wall_openings
from .palace_roof import roof_definition

LAYOUT_PATH = Path(__file__).resolve().parents[1] / "assets/buildings/godollo-palace/layout.json"
COMMON = {"corridor", "lobby", "stair_area"}


@lru_cache(maxsize=1)
def definition():
    with LAYOUT_PATH.open(encoding="utf-8") as stream:
        return json.load(stream)


def _polygons(geometry):
    if isinstance(geometry, Polygon):
        if geometry.area > .05:
            yield geometry
    else:
        for part in getattr(geometry, "geoms", ()):
            yield from _polygons(part)


def palace_frame(feature, polygon, config):
    """Return the authored frame only for the complete mapped palace footprint."""
    data = definition()
    if feature["id"] != data["osm_id"]:
        return None
    frame = coordinate_frame(config)
    anchors = np.array([frame["transformer"].transform(*p) for p in data["front_reference_lonlat"]])
    anchors -= frame["origin"]
    if any(polygon.boundary.distance(Point(p)) > .2 for p in anchors):
        raise ValueError("Palace footprint no longer matches the recorded street-facade anchors")
    u = anchors[1] - anchors[0]
    u /= np.linalg.norm(u)
    v = np.array([u[1], -u[0]])
    local = (np.array(polygon.exterior.coords) - anchors[0]) @ np.array([u, v]).T
    outline = Polygon(local)
    if polygon.interiors or outline.area < 2500:
        raise ValueError("The authored palace needs its complete, simple OSM footprint")
    return anchors[0], u, v, outline


def palace_tags(tags):
    data = definition()
    roof_height = max(sum(c[key] for key in ("drum_base_m", "drum_height_m", "dome_height_m", "lantern_height_m", "finial_height_m"))
                      for c in roof_definition()["cupolas"])
    return {**tags, "height": str(data["ceiling_m"] + roof_height),
            "roof:height": str(roof_height), "roof:shape": "hipped"}


def _rooms(outline, requested):
    remaining = outline
    rooms = []
    for label, kind, *bounds in requested:
        region = box(*bounds).intersection(remaining)
        for part in _polygons(region):
            if part.interiors:
                raise ValueError(f"Palace room {label} contains an unexpected hole")
            rooms.append({"label": label, "kind": kind, "polygon": np.array(part.exterior.coords)[:-1].tolist()})
        remaining = remaining.difference(region)
    # Unpartitioned circulation follows the actual shell, including skewed OSM
    # edges. Never fill a courtyard or bridge disconnected polygon components.
    for part in _polygons(remaining):
        if part.interiors:
            raise ValueError("Palace circulation contains an unexpected hole")
        rooms.append({"label": "Connecting gallery", "kind": "corridor",
                      "polygon": np.array(part.exterior.coords)[:-1].tolist()})
    # The slightly skewed OSM walls leave thin strips outside the rectangular
    # trace. They still receive the full shell floor, but cannot contain a
    # standing player and must not become isolated navigation rooms.
    return [room for room in rooms if not Polygon(room["polygon"]).buffer(-.4).is_empty]


def _wing_rooms(data):
    requested = []
    for label, x0, y0, x1, y1, axis, corridor, bay in data["rear_wings"]:
        # Rear-room boundaries are inferred from the museum board, with repeated
        # service bays where its photograph does not resolve individual walls.
        if axis == "x":
            requested.append([label + " corridor", "corridor", x0, corridor, x1, y1])
            for i, start in enumerate(np.arange(x0, x1, bay)):
                requested.append([f"{label} room {i + 1}", "living", float(start), y0,
                                  float(min(start + bay, x1)), corridor])
        else:
            requested.append([label + " corridor", "corridor", x0, y0, corridor, y1])
            for i, start in enumerate(np.arange(y0, y1, bay)):
                requested.append([f"{label} room {i + 1}", "living", corridor, float(start),
                                  x1, float(min(start + bay, y1))])
    return requested


def _partitions(rooms, storey, outline):
    result = []
    polygons = [Polygon(r["polygon"]) for r in rooms]
    for i, first in enumerate(rooms):
        for j in range(i + 1, len(rooms)):
            second = rooms[j]
            if first["kind"] in COMMON and second["kind"] in COMMON:
                continue
            shared = polygons[i].boundary.intersection(polygons[j].boundary)
            lines = [shared] if shared.geom_type == "LineString" else getattr(shared, "geoms", ())
            for line in lines:
                if line.geom_type != "LineString" or line.length < .02:
                    continue
                a, b = np.array(line.coords)[[0, -1]]
                axis = int(abs(b[0] - a[0]) > abs(b[1] - a[1]))
                if abs(a[axis] - b[axis]) > .005:
                    # Boolean clipping can leave submillimetre circulation
                    # slivers on a skewed facade; the exterior shell owns it.
                    if outline.boundary.buffer(.005).covers(line):
                        continue
                    raise ValueError("Authored partitions must follow the palace axes")
                low, high = sorted([float(a[1 - axis]), float(b[1 - axis])])
                width = min(1.25, high - low - .65)
                openings = []
                if width >= .85:
                    center = (low + high) / 2
                    openings.append([center - width / 2, center + width / 2])
                result.append({"axis": axis, "position": float(a[axis]), "low": low,
                               "high": high, "openings": openings, "storey": storey})
    return result


def prepare_layout(frame, floor):
    origin, u, v, outline = frame
    data = definition()
    rooms, partitions, stairs = [], [], []
    rear = _wing_rooms(data)
    for storey, height in enumerate(data["floor_heights_m"]):
        requested = data["ground_rooms"] if storey == 0 else data["upper_rooms"]
        level = _rooms(outline, requested + rear)
        for room in level:
            room["storey"] = storey
        partitions.extend(_partitions(level, storey, outline))
        rooms.extend(level)
    # Both documented ornamental stairs connect the two complete storeys.
    # Straight walkable ramps approximate their detailed stepped flights.
    for label, x0, y0, x1, y1 in data["stairs"]:
        ramp = box(x0, y0, x1, y1)
        if not outline.covers(ramp.buffer(.15)):
            raise ValueError(f"Palace staircase {label} leaves its shell")
        room = next(i for i, r in enumerate(rooms) if r["storey"] == 0 and r["label"] == label)
        stairs.append({"room": room, "ramp": [x0, y0, x1, y1], "opening": [x0, y0, x1, y1],
                       "axis": 1, "ascending": True, "bottom": 0.01, "top": data["floor_heights_m"][1]})
    return {"version": 1, "origin": [*origin.tolist(), floor], "u": [*u.tolist(), 0],
            "v": [*v.tolist(), 0], "ceiling": data["ceiling_m"] - .02,
            "upper_floors": data["floor_heights_m"][1:],
            "footprint": np.array(outline.exterior.coords)[:-1].tolist(),
            "rooms": rooms, "partitions": partitions, "stairs": stairs}


def _approaches(entrances, elevation):
    vertices, faces = [], []
    for center, direction, outward, floor, width in entrances:
        top = floor + .01
        ground = float(elevation(*center))
        run = max(2., (top - ground) * 2.0 + 1.)
        end = center + outward * run
        bottom = min(top, float(elevation(*end)) - .02)
        half = width / 2 + .4
        start = len(vertices)
        for xy, z in [(center - direction * half, top), (end - direction * half, bottom),
                      (end + direction * half, bottom), (center + direction * half, top)]:
            vertices.append([*xy, z])
        faces.extend([[start, start + 1, start + 2], [start, start + 2, start + 3]])
    if not vertices:
        return None
    mesh = trimesh.Trimesh(vertices=np.array(vertices, dtype=np.float32), faces=faces, process=False)
    mesh.visual = trimesh.visual.TextureVisuals(material=ENTRANCE_MATERIAL)
    return mesh


def palace_facade_details(polygon, wall, roof, elevation, frame):
    """Cut replacement doors and two rows of panes; never overlay old infills."""
    origin, u, v, outline = frame
    facades = _facades(polygon)
    data = definition()
    floor = float(np.float32(roof.vertices[:, 2].min() - data["ceiling_m"]))
    openings, approaches, occupied = {}, [], {}

    def install(kind, x, y, width, bottom, height, *, limit=2.5):
        point = origin + u * x + v * y
        index = min(range(len(facades)), key=lambda i: facades[i].line.distance(Point(point)))
        facade = facades[index]
        if facade.line.distance(Point(point)) > limit:
            return
        center = float(np.dot(point - facade.start, facade.direction))
        if facade.length < width + .36:
            return
        fitted = float(np.clip(center, width / 2 + .18, facade.length - width / 2 - .18))
        # OSM retains tiny bends in otherwise straight facades. Fit a leaf to
        # one planar span rather than silently losing the central courtyard
        # exit, or creating a bent panel that cannot use the hinge physics.
        if abs(fitted - center) > 1.5:
            return
        center = fitted
        span = box(center - width / 2 - .10, bottom, center + width / 2 + .10, bottom + height)
        if any(span.intersects(previous) for previous in occupied.get(index, ())):
            return
        occupied.setdefault(index, []).append(span)
        openings.setdefault(index, []).append((kind, _rectangle(facade, center, width, bottom, height)))
        if kind == "doors":
            approaches.append((facade.start + facade.direction * center, facade.direction, facade.normal, floor, width))

    for x, y, width in data["ground_exterior_doors"]:
        install("doors", x, y, width, floor, 3.2)
    candidates = [(x, -6.7 if 21.2 < x < 35.1 else 0) for x in data["main_front_window_centres"]]
    candidates.extend((x, y) for x in [0, 55.2] for y in data["wing_window_centres_y"])
    candidates.extend((x, y) for x in [10.5, 44.3] for y in [27, 31, 35, 39])
    candidates.extend((x, 24.7) for x in data["courtyard_window_centres_x"])
    # Photograph-derived repeated facade bays continue along the rear wings.
    # Main-body positions above are explicitly traced from the floorplan.
    for facade in facades:
        a = np.array([np.dot(facade.start - origin, u), np.dot(facade.start - origin, v)])
        b = np.array([np.dot(facade.end - origin, u), np.dot(facade.end - origin, v)])
        if min(a[1], b[1]) < 40.5 or facade.length < 2:
            continue
        count = max(1, round(facade.length / 3.2))
        candidates.extend(tuple(a + (b - a) * ((i + .5) / count)) for i in range(count))
    for level in data["floor_heights_m"]:
        for x, y in candidates:
            install("windows", x, y, 1.15, floor + level + .9, 2.5 if level == 0 else 2.8)
    detailed, doors, windows, crawl = cut_wall_openings(wall, facades, openings)
    wall.metadata["building_floor"] = [*origin.tolist(), floor]
    wall.metadata["authored_floorplan"] = prepare_layout(frame, floor)
    return (detailed, doors, windows, _approaches(approaches, elevation), crawl, None, None,
            {"doors": sum(k == "doors" for entries in openings.values() for k, _ in entries),
             "windows": sum(k == "windows" for entries in openings.values() for k, _ in entries)})
