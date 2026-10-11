"""Refine foundation caps where door corners meet their boundary.

Rounded boundary points can cross the internal edge of a very thin triangle.
Join that triangle to a neighbor before triangulation, rather than creating an
inverted face. Unaffected cap triangles retain their original geometry.
"""

import numpy as np
from shapely import constrained_delaunay_triangles
from shapely.geometry import Polygon

from .facade_surface import edge_key


def _boundary(indices, caps, splits):
    edges = {}
    for index in indices:
        triangle = caps[index][0]
        for start, end in zip(triangle, np.roll(triangle, -1, axis=0)):
            key = edge_key(start, end)
            if key in edges:
                del edges[key]
            else:
                edges[key] = (start, end)
    following = {tuple(start): end for start, end in edges.values()}
    rings = []
    while following:
        first = next(iter(following))
        current, ring = first, []
        while True:
            start = np.asarray(current)
            end = following.pop(current)
            ring.append(start)
            points = list(splits.get(edge_key(start, end), {}).values())
            points.sort(key=lambda point: float((point - start) @ (end - start)))
            ring.extend(points)
            current = tuple(end)
            if current == first:
                break
        rings.append(np.asarray(ring))
    rings.sort(key=lambda ring: Polygon(ring[:, :2]).area, reverse=True)
    return Polygon(rings[0][:, :2], [ring[:, :2] for ring in rings[1:]]), np.vstack(rings)


def refined_caps(caps, splits):
    """Yield cap triangles and normals, retaining every outer boundary sample."""
    adjacent = {}
    for index, (triangle, _) in enumerate(caps):
        for start, end in zip(triangle, np.roll(triangle, -1, axis=0)):
            adjacent.setdefault(edge_key(start, end), []).append(index)
    patches = {index: {index} for index in range(len(caps))}
    owner = list(patches)
    for index in range(len(caps)):
        if index not in patches:
            continue
        while True:
            polygon, vertices = _boundary(patches[index], caps, splits)
            if polygon.is_valid and polygon.area > 0:
                break
            # Prefer removing a long internal edge: tiny roof splits create
            # narrow cap triangles alongside much larger neighbors.
            candidates = []
            for edge, neighbors in adjacent.items():
                inside = [neighbor in patches[index] for neighbor in neighbors]
                if len(neighbors) == 2 and sum(inside) == 1:
                    other = neighbors[inside.index(False)]
                    length = np.linalg.norm(np.asarray(edge[1]) - edge[0])
                    candidates.append((length, owner[other]))
            if not candidates:
                raise ValueError("Cannot triangulate the refined foundation boundary")
            other = max(candidates)[1]
            merged = patches.pop(other)
            patches[index].update(merged)
            for member in merged:
                owner[member] = index
    for indices in patches.values():
        polygon, vertices = _boundary(indices, caps, splits)
        if len(indices) == 1 and len(vertices) == 3:
            yield caps[next(iter(indices))]
            continue
        lookup = {tuple(point[:2]): point for point in vertices}
        for face in constrained_delaunay_triangles(polygon).geoms:
            triangle = np.array([lookup[tuple(xy)] for xy in np.asarray(face.exterior.coords)[:3]])
            normal = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
            if normal[2] > 0:
                triangle, normal = triangle[::-1], -normal
            yield triangle, normal / np.linalg.norm(normal)
