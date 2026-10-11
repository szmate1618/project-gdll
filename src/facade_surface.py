"""Project a sampled facade to 2D while retaining its exported 3D boundary.

Coordinates are local east/north/up meters. Along-wall coordinates stay local
and in float64, so micrometer-long roof splits survive kilometer-sized maps.
The lift follows the original float32 horizontal boundary samples, including
small bends introduced by export rounding.
"""

import numpy as np
from shapely import union_all
from shapely.geometry import Polygon


def edge_key(start, end):
    return tuple(sorted((tuple(start), tuple(end))))


class FacadeMatcher:
    def __init__(self, facades):
        self.starts = np.array([facade.start for facade in facades])
        self.directions = np.array([facade.direction for facade in facades])
        self.lengths = np.array([facade.length for facade in facades])
        self.normals = np.array([facade.normal for facade in facades])

    def match(self, endpoints, normal):
        # Short roof split edges can rotate on export. Match their positions
        # first and use normal alignment to distinguish adjacent corner sides.
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


class FacadeSurface:
    def __init__(self, facade, triangles):
        self.facade = facade
        self.normal = np.r_[facade.normal, 0.0]
        points = np.unique(np.asarray(triangles).reshape(-1, 3), axis=0)
        xy, inverse = np.unique(points[:, :2], axis=0, return_inverse=True)
        neighbors = {tuple(point): set() for point in xy}
        for triangle in triangles:
            endpoints = np.unique(triangle[:, :2], axis=0)
            if len(endpoints) != 2:
                raise ValueError("Facade triangle must connect two vertical boundary samples")
            start, end = map(tuple, endpoints)
            neighbors[start].add(end)
            neighbors[end].add(start)
        ends = [point for point, linked in neighbors.items() if len(linked) == 1]
        if len(ends) != 2 or any(len(linked) > 2 for linked in neighbors.values()):
            raise ValueError("Facade boundary must form a single sampled chain")
        current = min(ends, key=lambda point: np.linalg.norm(np.asarray(point) - facade.start))
        chain, previous = [], None
        while current is not None:
            chain.append(current)
            following = neighbors[current] - {previous}
            previous, current = current, next(iter(following), None)
        if len(chain) != len(xy):
            raise ValueError("Facade boundary has disconnected samples")
        self.xy = np.asarray(chain)
        self.deltas = np.diff(self.xy, axis=0)
        self.lengths = np.linalg.norm(self.deltas, axis=1)
        # Float32 roof splits can briefly run backwards along the nominal
        # wall direction. Arc length retains their actual boundary order.
        self.coordinates = np.r_[0., np.cumsum(self.lengths)]
        lower = np.full(len(xy), np.inf)
        np.minimum.at(lower, inverse, points[:, 2])
        heights = {tuple(point): height for point, height in zip(xy, lower)}
        self.lower = np.column_stack([self.xy, [heights[point] for point in chain]])
        self.vertices = {tuple(self.project(point)): point for point in points}
        self.boundary_points = np.array(list(self.vertices))
        self.outline = union_all([Polygon(self.project(triangle)) for triangle in triangles])
        self.bottom_splits = {}

    def along(self, xy):
        xy = np.asarray(xy)
        delta = xy[..., None, :] - self.xy[:-1]
        fractions = np.clip(np.sum(delta * self.deltas, axis=-1) / self.lengths ** 2, 0., 1.)
        nearest = self.xy[:-1] + fractions[..., None] * self.deltas
        segments = np.argmin(np.sum((xy[..., None, :] - nearest) ** 2, axis=-1), axis=-1)
        fraction = np.take_along_axis(fractions, np.asarray(segments)[..., None], axis=-1)[..., 0]
        return self.coordinates[segments] + fraction * self.lengths[segments]

    def project(self, points):
        points = np.asarray(points)
        return np.stack([self.along(points[..., :2]), points[..., 2]], axis=-1)

    def snap(self, uv):
        """Return a canonical 2D key for an exported-precision 3D vertex."""
        uv = np.asarray(uv)
        key = tuple(uv)
        if key in self.vertices:
            return uv
        along, height = uv
        segment = int(np.clip(np.searchsorted(self.coordinates, along, side='right') - 1,
                              0, len(self.coordinates) - 2))
        start, end = self.lower[segment:segment + 2]
        fraction = (along - self.coordinates[segment]) / (self.coordinates[segment + 1] - self.coordinates[segment])
        xy = start[:2] + fraction * (end[:2] - start[:2])
        vertex = np.r_[xy, height].astype(np.float32).astype(float)
        base = start[2] + fraction * (end[2] - start[2])
        on_base = abs(height - base) <= 1e-8
        if on_base:
            # Rounding can reach or cross an existing sample on a tiny roof
            # split. Keep the foundation order and snap its height as well.
            rounded_along = self.along(vertex[:2])
            for endpoint, crossed in ((start, rounded_along <= self.coordinates[segment]),
                                      (end, rounded_along >= self.coordinates[segment + 1])):
                if crossed or np.array_equal(vertex[:2], endpoint[:2]):
                    vertex = endpoint
                    break
            if not any(np.array_equal(vertex, endpoint) for endpoint in (start, end)):
                self.bottom_splits.setdefault(edge_key(start, end), {})[tuple(vertex)] = vertex
        canonical = self.project(vertex)
        self.vertices.setdefault(tuple(canonical), vertex)
        return canonical
