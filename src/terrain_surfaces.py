"""Triangulate bounded surfaces in local east/north/up metres.

Roads and railway beds share terrain sampling and ridge clearance. The scene
exporter handles the final rotation into glTF's Y-up coordinate frame.
"""

from __future__ import annotations

import math
from typing import Callable

import numpy as np
from shapely.geometry import box
from shapely.geometry.base import BaseGeometry
import trimesh


def geometry_parts(geometry: BaseGeometry, geom_type: str):
    """Yield components of the requested type from nested Shapely geometry."""
    if geometry.is_empty:
        return
    if geometry.geom_type == geom_type:
        yield geometry
    elif hasattr(geometry, "geoms"):
        for part in geometry.geoms:
            yield from geometry_parts(part, geom_type)


def _surface_cells(polygon: BaseGeometry, spacing: float):
    """Clip a surface to a metric grid before triangulation.

    Triangulating a kilometre-long strip and then uniformly subdividing creates
    many tiny, thin triangles. Vertical strips followed by occupied cells keep
    work proportional to length and also preserve concavities and holes.
    Every output triangle is at most sqrt(2) * spacing metres along an edge.
    """
    xmin, ymin, xmax, ymax = polygon.bounds
    for column in range(math.floor(xmin / spacing), math.ceil(xmax / spacing)):
        left = column * spacing
        strip = polygon.intersection(box(left, ymin - 1, left + spacing, ymax + 1))
        for component in geometry_parts(strip, "Polygon"):
            cymin, cymax = component.bounds[1], component.bounds[3]
            for row in range(math.floor(cymin / spacing), math.ceil(cymax / spacing)):
                bottom = row * spacing
                cell = component.intersection(box(left, bottom, left + spacing, bottom + spacing))
                for part in geometry_parts(cell, "Polygon"):
                    if part.area > 1e-8:
                        yield part


def surface_mesh(polygon: BaseGeometry, elevation: Callable, spacing: float, offset: float):
    """Sample and triangulate a polygon above the rendered terrain surface.

    ``elevation(x, y)`` receives coordinate arrays and returns local up metres.
    Grid spacing bounds triangle size; offset is the clearance above terrain.
    Callers validate spacing and offset before invoking this shared operation.
    """
    vertices_parts = []
    faces_parts = []
    vertex_count = 0
    for component in geometry_parts(polygon, "Polygon"):
        for cell in _surface_cells(component, spacing):
            xy, faces = trimesh.creation.triangulate_polygon(cell, engine="earcut")
            if not len(faces):
                continue
            vertices_parts.append(xy)
            faces_parts.append(faces + vertex_count)
            vertex_count += len(xy)
    if not vertices_parts:
        return None
    xy = np.vstack(vertices_parts)
    z = np.broadcast_to(np.asarray(elevation(xy[:, 0], xy[:, 1]), dtype=float), (len(xy),))
    if not np.all(np.isfinite(z)):
        raise ValueError("Terrain returned non-finite elevations for surface vertices")
    mesh = trimesh.Trimesh(
        vertices=np.column_stack((xy, z + offset)),
        faces=np.vstack(faces_parts),
        process=False,
    )
    _raise_over_terrain(mesh, elevation, offset)
    # Face winding should be consistent even if clipping returns clockwise rings.
    downward = mesh.face_normals[:, 2] < 0
    if np.any(downward):
        mesh.faces[downward] = mesh.faces[downward, ::-1]
    return mesh


def _raise_over_terrain(mesh: trimesh.Trimesh, elevation: Callable, offset: float) -> None:
    """Keep interiors above terrain triangle bends, sharing seam corrections.

    A surface triangle may cross a terrain ridge despite all its vertices
    clearing the ground. Probe edge midpoints and the centroid, then lift each
    incident vertex enough to keep these points at the requested offset.
    Duplicate XY vertices on cell boundaries receive the same correction.
    """
    triangles = mesh.triangles
    probes = np.stack((triangles.mean(axis=1), (triangles[:, 0] + triangles[:, 1]) / 2,
                       (triangles[:, 1] + triangles[:, 2]) / 2,
                       (triangles[:, 2] + triangles[:, 0]) / 2), axis=1)
    ground = np.asarray(elevation(probes[:, :, 0], probes[:, :, 1]))
    if not np.all(np.isfinite(ground)):
        raise ValueError("Terrain returned non-finite elevations for surface interiors")
    needed = np.maximum(0, (ground + offset - probes[:, :, 2]).max(axis=1))
    if np.max(needed) < 1e-8:
        return
    # Clip computations may differ below floating point export precision.
    xy = mesh.vertices[:, :2].astype(np.float32)
    _, inverse = np.unique(xy, axis=0, return_inverse=True)
    correction = np.zeros(inverse.max() + 1)
    np.maximum.at(correction, inverse[mesh.faces].ravel(), np.repeat(needed, 3))
    mesh.vertices[:, 2] += correction[inverse]
