"""Sample and simplify terrain-following lines in local east/north/up meters."""
from __future__ import annotations

import numpy as np

from .terrain import Terrain


def _terrain_fractions(a, b, axes):
    """Locate grid and diagonal crossings, including float32 exported axes."""
    # Work in east/south so both terrain axes are increasing.
    start, end = a * [1, -1], b * [1, -1]
    fractions = [0., 1.]
    for coordinate, axis in enumerate(axes):
        delta = end[coordinate] - start[coordinate]
        if abs(delta) < 1e-12:
            continue
        lo, hi = sorted((start[coordinate], end[coordinate]))
        first, last = np.searchsorted(axis, lo, side="right"), np.searchsorted(axis, hi)
        fractions.extend((axis[first:last] - start[coordinate]) / delta)
    edges = np.unique(fractions)
    for left, right in zip(edges[:-1], edges[1:]):
        midpoint = start + (end - start) * ((left + right) / 2)
        cell = [int(np.clip(np.searchsorted(axis, value, side="right") - 1,
                            0, len(axis) - 2)) for axis, value in zip(axes, midpoint)]
        origin = np.array([axis[index] for axis, index in zip(axes, cell)])
        size = np.array([axis[index + 1] - axis[index] for axis, index in zip(axes, cell)])
        # Within this cell, v-u is linear and zero on its NW-to-SE diagonal.
        points = start + np.array([left, right])[:, None] * (end - start)
        points = np.clip(points, [axis[0] for axis in axes], [axis[-1] for axis in axes])
        local = (points - origin) / size
        diagonal = local[:, 1] - local[:, 0]
        delta = diagonal[1] - diagonal[0]
        if abs(delta) > 1e-12:
            fraction = left - diagonal[0] * (right - left) / delta
            if left < fraction < right:
                fractions.append(fraction)
    return sorted(fractions)


def sample_terrain_line(line, elevation):
    """Sample mapped vertices and every rendered terrain slope boundary."""
    coords = np.asarray(line.coords, dtype=float)
    axes = None
    if isinstance(elevation, Terrain):
        x_axis = (elevation.x_axis if hasattr(elevation, "x_axis") else
                  np.linspace(elevation.bounds[0], elevation.bounds[2], elevation.cols))
        y_axis = (elevation.y_axis if hasattr(elevation, "y_axis") else
                  np.linspace(elevation.bounds[3], elevation.bounds[1], elevation.rows))
        axes = (x_axis, -y_axis)
    xy, distances = [coords[0]], [0.0]
    distance = 0.0
    for a, b in zip(coords[:-1], coords[1:]):
        length = float(np.linalg.norm(b - a))
        if length < 1e-7:
            continue
        fractions = _terrain_fractions(a, b, axes) if axes is not None else [0., 1.]
        previous = 0.0
        for fraction in sorted(fractions):
            if fraction <= previous or ((fraction - previous) * length < 1e-9 and fraction < 1):
                continue
            xy.append(a + fraction * (b - a))
            distances.append(distance + fraction * length)
            previous = fraction
        distance += length
    return np.asarray(xy), np.asarray(distances)


def profile_corners(points):
    """Merge straight 3D runs; retain bends, reversals and changes of slope."""
    steps = np.diff(points, axis=0)
    directions = steps / np.linalg.norm(steps, axis=1)[:, None]
    keep = [0]
    direction = directions[0]
    for index in range(1, len(directions)):
        # Compare to the start of the run so small changes cannot accumulate.
        # This angular tolerance only absorbs numerical interpolation noise
        # (at most 1 micrometre of drift over a 10 km straight run).
        if np.linalg.norm(directions[index] - direction) > 1e-10:
            keep.append(index)
            direction = directions[index]
    keep.append(len(points) - 1)
    return keep


