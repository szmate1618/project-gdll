"""Partition existing wall triangles into masonry and flush door/glass infills.

No boolean engine or new building shell is needed. Every source triangle is
partitioned in its vertical plane, preserving roof, foundation and courtyard
boundary edges. All returned vertices use the GLB's float32 coordinates.
"""

import numpy as np
from shapely import constrained_delaunay_triangles, make_valid, union_all
from shapely.geometry import Polygon
import trimesh


def _polygons(geometry):
    if isinstance(geometry, Polygon):
        yield geometry
    else:
        for part in getattr(geometry, "geoms", ()):
            yield from _polygons(part)


def cut_wall_openings(wall, facades, openings):
    """Return three meshes: remaining walls, door infills, and glass infills.

    ``openings`` maps facade indices to (kind, four world-space corners) pairs.
    Coordinates are generator-local east/north/up meters. Infill triangles share
    their boundary vertices with masonry, including original diagonal splits.
    """
    triangles = {kind: [] for kind in ("walls", "doors", "windows")}
    normals = {kind: [] for kind in triangles}
    cutters = {}
    for index, entries in openings.items():
        axis = int(np.argmax(np.abs(facades[index].direction)))
        grouped = {kind: union_all([Polygon(rectangle[:, [axis, 2]]) for label, rectangle in entries
                                   if label == kind]) for kind in ("doors", "windows")}
        cutters[index] = (axis, grouped, union_all(list(grouped.values())))

    for triangle, normal in zip(wall.triangles.astype(np.float32).astype(float), wall.face_normals):
        # Facade triangles are vertical. Terrain-seated bottom caps can be
        # steep and must not be mistaken for facades by a slope threshold.
        if abs(normal[2]) > 1e-6:
            triangles["walls"].append(triangle)
            normals["walls"].append(normal)
            continue
        center = triangle[:, :2].mean(axis=0)
        candidates = [index for index, facade in enumerate(facades)
                      if np.dot(normal[:2], facade.normal) > .99]
        if not candidates:
            raise ValueError("Cannot associate building wall with a facade")
        index = min(candidates, key=lambda candidate: _segment_distance(center, facades[candidate]))
        if index not in cutters:
            triangles["walls"].append(triangle)
            normals["walls"].append(normal)
            continue
        axis, grouped, all_cutters = cutters[index]
        source = Polygon(triangle[:, [axis, 2]])
        if not source.intersects(all_cutters):
            triangles["walls"].append(triangle)
            normals["walls"].append(normal)
            continue
        # The two distinct horizontal endpoints determine the exact source
        # plane. Canonical order gives identical interpolation on shared edges.
        endpoints = np.unique(triangle[:, :2], axis=0)
        endpoints = endpoints[np.argsort(endpoints[:, axis])]
        start, end = endpoints[0], endpoints[-1]
        for kind, geometry in [("walls", source.difference(all_cutters)),
                               *[(kind, source.intersection(cutter)) for kind, cutter in grouped.items()]]:
            for polygon in _polygons(geometry):
                # Quantize partition boundaries together before triangulation;
                # identical cut edges must survive export with identical points.
                shell = np.asarray(polygon.exterior.coords).astype(np.float32).astype(float)
                holes = [np.asarray(ring.coords).astype(np.float32).astype(float) for ring in polygon.interiors]
                for snapped in _polygons(make_valid(Polygon(shell, holes))):
                    for face in constrained_delaunay_triangles(snapped).geoms:
                        uv = np.asarray(face.exterior.coords)[:3]
                        fraction = (uv[:, 0] - start[axis]) / (end[axis] - start[axis])
                        xy = start + fraction[:, None] * (end - start)
                        xy[:, axis] = uv[:, 0]
                        vertices = np.column_stack([xy, uv[:, 1]]).astype(np.float32).astype(float)
                        cross = np.cross(vertices[1] - vertices[0], vertices[2] - vertices[0])
                        if np.dot(cross, normal) < 0:
                            vertices = vertices[::-1]
                        if np.linalg.norm(cross) == 0:
                            continue
                        triangles[kind].append(vertices)
                        normals[kind].append(normal)

    result = []
    for kind, faces in triangles.items():
        if not faces:
            result.append(None)
            continue
        # Independent face vertices retain flat facade normals. Solid validation
        # merges coincident positions across meshes to check the material seams.
        mesh = trimesh.Trimesh(vertices=np.asarray(faces).reshape(-1, 3),
                               faces=np.arange(len(faces) * 3).reshape(-1, 3), process=False)
        mesh.vertex_normals = np.repeat(normals[kind], 3, axis=0)
        result.append(mesh)
    return tuple(result)


def _segment_distance(point, facade):
    along = np.clip(np.dot(point - facade.start, facade.direction), 0, facade.length)
    return np.linalg.norm(point - (facade.start + along * facade.direction))
