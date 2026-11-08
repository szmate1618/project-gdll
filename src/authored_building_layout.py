"""Clipped room fitting, shared-boundary partitions and exterior approaches.

Authored layouts use local plan metres. These helpers contain no building
identity, source definition or configured geographic origin.
"""
import numpy as np
from shapely.geometry import LineString, Polygon, box
from shapely.ops import split
import trimesh
from .building_entrances import ENTRANCE_MATERIAL

COMMON = {"corridor", "lobby", "stair_area"}


def _polygons(geometry):
    if isinstance(geometry, Polygon):
        if geometry.area > .05:
            yield geometry
    else:
        for part in getattr(geometry, "geoms", ()):
            yield from _polygons(part)


def _room_parts(geometry):
    """Open courtyard rings with a straight cut into simple room polygons."""
    for part in _polygons(geometry):
        if not part.interiors:
            yield part
            continue
        x = Polygon(part.interiors[0]).centroid.x
        _, low, _, high = part.bounds
        for piece in split(part, LineString([(x, low - 1), (x, high + 1)])).geoms:
            yield from _room_parts(piece)


def _rooms(outline, requested):
    remaining = outline
    rooms = []
    for label, kind, *bounds in requested:
        region = box(*bounds).intersection(remaining)
        for part in _room_parts(region):
            rooms.append({"label": label, "kind": kind, "polygon": np.array(part.exterior.coords)[:-1].tolist()})
        remaining = remaining.difference(region)
    # Unpartitioned circulation follows the actual shell, including skewed OSM
    # edges. Never fill a courtyard or bridge disconnected polygon components.
    for part in _polygons(remaining):
        if part.interiors:
            raise ValueError("Authored circulation contains an unexpected hole")
        rooms.append({"label": "Connecting gallery", "kind": "corridor",
                      "polygon": np.array(part.exterior.coords)[:-1].tolist()})
    # The slightly skewed OSM walls leave thin strips outside the rectangular
    # trace. They still receive the full shell floor, but cannot contain a
    # standing player and must not become isolated navigation rooms.
    return [room for room in rooms if not Polygon(room["polygon"]).buffer(-.4).is_empty]


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
                    raise ValueError("Authored partitions must follow the authored axes")
                low, high = sorted([float(a[1 - axis]), float(b[1 - axis])])
                width = min(1.25, high - low - .65)
                openings = []
                if width >= .85:
                    center = (low + high) / 2
                    openings.append([center - width / 2, center + width / 2])
                result.append({"axis": axis, "position": float(a[axis]), "low": low,
                               "high": high, "openings": openings, "storey": storey})
    return result


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
