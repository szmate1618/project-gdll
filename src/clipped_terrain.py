"""Sample published terrain triangles after basement footprints have been removed."""

import numpy as np
from shapely import points, polygons
from shapely.strtree import STRtree

from .terrain import Terrain


class ClippedTerrain(Terrain):
    """Retain the source grid for metadata; return NaN where no surface remains.

    Coordinates and elevations are local east/north/up metres. The spatial index
    contains only active exported triangles, including their float32 rounding.
    """

    def __init__(self, grid, positions, indices):
        if indices.size % 3 or not indices.size or indices.max() >= len(positions):
            raise ValueError("Map terrain has invalid clipped triangle indices.")
        triangles = positions[indices.reshape(-1, 3)].astype(float)
        centers = triangles[:, :, :2].mean(axis=1)
        dx, dy = np.diff(grid.x_axis), -np.diff(grid.y_axis)
        i = np.clip(np.searchsorted(grid.x_axis, centers[:, 0], side='right') - 1, 0, grid.cols - 2)
        j = np.clip(np.searchsorted(-grid.y_axis, -centers[:, 1], side='right') - 1, 0, grid.rows - 2)
        u = (triangles[:, :, 0] - grid.x_axis[i, None]) / dx[i, None]
        v = (grid.y_axis[j, None] - triangles[:, :, 1]) / dy[j, None]
        # Clipping rounds new boundary vertices to float32 before publication.
        rounding = 4 * abs(float(np.spacing(np.float32(np.abs(positions).max()))))
        cell_tolerance = rounding / min(dx.min(), dy.min())
        if (np.any(u < -cell_tolerance) or np.any(u > 1 + cell_tolerance)
                or np.any(v < -cell_tolerance) or np.any(v > 1 + cell_tolerance)
                or not np.all(np.all(v >= u - cell_tolerance, axis=1)
                              | np.all(u >= v - cell_tolerance, axis=1))):
            raise ValueError("Map terrain topology must retain the NW-to-SE grid triangle planes.")
        gradient = max(np.max(np.abs(np.diff(grid.heights, axis=1) / dx)),
                       np.max(np.abs(np.diff(grid.heights, axis=0) / dy[:, None])))
        if np.any(np.abs(positions[:, 2] - grid(positions[:, 0], positions[:, 1]))
                  > rounding * (1 + 2 * gradient)):
            raise ValueError("Map clipped terrain vertices do not match the published height-grid planes.")
        normal = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
        if np.any(normal[:, 2] <= 0):
            raise ValueError("Map clipped terrain triangles must have upward winding and nonzero area.")
        super().__init__(grid.heights, grid.bounds, grid.datum)
        self.x_axis, self.y_axis = grid.x_axis, grid.y_axis
        self._origins = triangles[:, 0]
        self._slopes = -normal[:, :2] / normal[:, 2, None]
        self._index = STRtree(polygons(triangles[:, :, :2]))

    def __call__(self, x, y):
        x, y = np.broadcast_arrays(np.asarray(x, dtype=float), np.asarray(y, dtype=float))
        xy = np.column_stack((np.clip(x.ravel(), self.x_axis[0], self.x_axis[-1]),
                              np.clip(y.ravel(), self.y_axis[-1], self.y_axis[0])))
        samples, faces = self._index.query(points(xy), predicate='intersects')
        origins = self._origins[faces]
        heights = origins[:, 2] + np.sum(self._slopes[faces] * (xy[samples] - origins[:, :2]), axis=1)
        values = np.full(x.size, -np.inf)
        np.maximum.at(values, samples, heights)
        values[values == -np.inf] = np.nan
        values = values.reshape(x.shape)
        return float(values) if values.ndim == 0 else values
