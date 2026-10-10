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
    facade_matcher = _FacadeMatcher(facades)
    cutters = {}
    for index, entries in openings.items():
        for axis in (0, 1):
            # Cut in the exported precision too: a window edge nearly on a
            # foundation split must snap before clipping, not leave a sliver
            # that collapses afterward and makes mismatched boundary edges.
            grouped = {}
            for kind in ("doors", "windows"):
                projected = [rectangle[:, [axis, 2]].astype(np.float32).astype(float)
                             for label, rectangle in entries if label == kind]
                grouped[kind] = union_all([Polygon(vertices) for vertices in projected])
            cutters[index, axis] = (grouped, union_all(list(grouped.values())))

    for triangle, normal in zip(wall.triangles.astype(np.float32).astype(float), wall.face_normals):
        # Facade triangles are vertical. Terrain-seated bottom caps can be
        # steep and must not be mistaken for facades by a slope threshold.
        if abs(normal[2]) > 1e-6:
            triangles["walls"].append(triangle)
            normals["walls"].append(normal)
            continue
        endpoints = np.unique(triangle[:, :2], axis=0)
        index = facade_matcher.match(endpoints, normal)
        # Very short roof/foundation split edges can rotate after float32
        # rounding. Project in the actual triangle's strongest horizontal axis.
        axis = int(np.argmax(np.ptp(endpoints, axis=0)))
        if (index, axis) not in cutters:
            triangles["walls"].append(triangle)
            normals["walls"].append(normal)
            continue
        grouped, all_cutters = cutters[index, axis]
        source = Polygon(triangle[:, [axis, 2]])
        if not source.intersects(all_cutters):
            triangles["walls"].append(triangle)
            normals["walls"].append(normal)
            continue
        # The two distinct horizontal endpoints determine the exact source
        # plane. Canonical order gives identical interpolation on shared edges.
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


class _FacadeMatcher:
    def __init__(self, facades):
        self.starts = np.array([facade.start for facade in facades])
        self.directions = np.array([facade.direction for facade in facades])
        self.lengths = np.array([facade.length for facade in facades])
        self.normals = np.array([facade.normal for facade in facades])

    def match(self, endpoints, normal):
        # Normals of micrometer-long roof split edges can rotate on export.
        # Require both endpoints to lie on one original facade within float32
        # precision, then use alignment to distinguish adjacent corner sides.
        magnitude = float(np.max(np.abs(endpoints)))
        tolerance = max(2e-5, 4 * float(np.spacing(np.float32(magnitude))))
        delta = endpoints[:, None, :] - self.starts
        along = np.clip(np.sum(delta * self.directions, axis=2), 0, self.lengths)
        nearest = self.starts + along[:, :, None] * self.directions
        distance = np.max(np.linalg.norm(endpoints[:, None, :] - nearest, axis=2), axis=0)
        candidates = np.flatnonzero(distance <= tolerance)
        if not len(candidates):
            center = endpoints.mean(axis=0)
            raise ValueError(f"Cannot associate building wall at ({center[0]:.6f}, {center[1]:.6f}) with a facade")
        alignment = self.normals @ normal[:2]
        return min(candidates, key=lambda index: (-alignment[index], distance[index]))
