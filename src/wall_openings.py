"""Cut whole facade outlines into indexed masonry, doors and glass.

Original wall diagonals do not constrain the openings. Roof boundary vertices
stay unchanged; the near-LoD bottom cap gains matching door-base vertices.
Every output vertex uses float32 export precision and retains a flat normal.
"""

import numpy as np
from shapely import constrained_delaunay_triangles, make_valid, union_all
from shapely.geometry import Polygon, box
import trimesh

from .building_caps import refined_caps
from .facade_surface import FacadeMatcher as _FacadeMatcher, FacadeSurface


def _polygons(geometry):
    if isinstance(geometry, Polygon):
        if not geometry.is_empty:
            yield geometry
    else:
        for part in getattr(geometry, "geoms", ()):
            yield from _polygons(part)


def _insert_vertices(ring, points):
    """Keep collinear constraints, including vertices shared by material seams."""
    ring = np.asarray(ring)
    result = []
    for start, end in zip(ring[:-1], ring[1:]):
        result.append(start)
        delta = end - start
        length_squared = float(delta @ delta)
        if length_squared == 0:
            continue
        fractions = (points - start) @ delta / length_squared
        error = np.linalg.norm(points - (start + fractions[:, None] * delta), axis=1)
        inside = (fractions > 1e-10) & (fractions < 1 - 1e-10) & (error <= 1e-9)
        indices = np.flatnonzero(inside)
        result.extend(points[indices[np.argsort(fractions[indices])]])
    return np.asarray([*result, result[0]])


def _partition(surface, entries, band_top):
    cutters = {}
    for kind in ('doors', 'windows'):
        rectangles = [Polygon(surface.project(rectangle.astype(np.float32).astype(float)))
                      for label, rectangle in entries if label == kind]
        cutters[kind] = union_all(rectangles)
    all_cutters = union_all(list(cutters.values()))
    regions = [('walls', surface.outline.difference(all_cutters)),
               *[(kind, surface.outline.intersection(cutter)) for kind, cutter in cutters.items()]]
    # Below the first window sill, retain the foundation's vertical strips.
    # Very short roof splits can bend backwards at float32 precision. A whole
    # facade triangulation there can create an ear that overlaps the bottom
    # cap. These strips keep the lower wall a ruled vertical surface, while
    # windows above the sill remain simple, unsplit rectangles.
    min_u, min_z, max_u, max_z = surface.outline.bounds
    band = box(min_u - 1, min_z - 1, max_u + 1, band_top)
    bands = [box(left, min_z - 1, right, band_top)
             for left, right in zip(surface.coordinates[:-1], surface.coordinates[1:])]
    regions = [(kind, part) for kind, geometry in regions
               for part in [geometry.difference(band),
                            *[geometry.intersection(strip) for strip in bands]]]
    pieces = []
    for kind, geometry in regions:
        for polygon in _polygons(geometry):
            shell = [surface.snap(uv) for uv in polygon.exterior.coords]
            holes = [[surface.snap(uv) for uv in ring.coords] for ring in polygon.interiors]
            pieces.extend((kind, part) for part in _polygons(make_valid(Polygon(shell, holes))))
    # Boolean operations may omit collinear vertices from one side of a seam.
    # Constrain both material partitions with the same canonical point set.
    points = [surface.boundary_points]
    for _, polygon in pieces:
        points.extend(np.asarray(ring.coords) for ring in [polygon.exterior, *polygon.interiors])
    points = np.unique(np.vstack(points), axis=0)
    for kind, polygon in pieces:
        shell = _insert_vertices(polygon.exterior.coords, points)
        holes = [_insert_vertices(ring.coords, points) for ring in polygon.interiors]
        for face in constrained_delaunay_triangles(Polygon(shell, holes)).geoms:
            yield kind, np.array([surface.vertices[tuple(uv)] for uv in np.asarray(face.exterior.coords)[:3]])


def _indexed_mesh(triangles, normals):
    if not triangles:
        return None
    positions = np.asarray(triangles).reshape(-1, 3).astype(np.float32)
    normals = np.repeat(normals, 3, axis=0).astype(np.float32)
    attributes = np.column_stack([positions, normals])
    # Signed zero has identical rendering semantics; give it one index too.
    attributes[attributes == 0] = 0
    keys = attributes.view(np.dtype(('V', attributes.shape[1] * attributes.dtype.itemsize))).ravel()
    _, first, inverse = np.unique(keys, return_index=True, return_inverse=True)
    mesh = trimesh.Trimesh(vertices=positions[first].astype(float), faces=inverse.reshape(-1, 3), process=False)
    mesh.vertex_normals = normals[first].astype(float)
    return mesh


def cut_wall_openings(wall, facades, openings):
    """Return indexed masonry, door and window meshes in east/north/up meters."""
    triangles = {kind: [] for kind in ('walls', 'doors', 'windows')}
    normals = {kind: [] for kind in triangles}
    matcher = _FacadeMatcher(facades)
    groups, caps = {}, []
    for triangle, normal in zip(wall.triangles.astype(np.float32).astype(float), wall.face_normals):
        if abs(normal[2]) > 1e-6:
            caps.append((triangle, normal))
        else:
            index = matcher.match(np.unique(triangle[:, :2], axis=0), normal)
            groups.setdefault(index, []).append((triangle, normal))

    def append(kind, triangle, normal):
        cross = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
        if np.dot(cross, normal) < 0:
            triangle = triangle[::-1]
        if np.linalg.norm(cross) == 0:
            return
        triangles[kind].append(triangle)
        normals[kind].append(normal)

    splits = {}
    entries = [entry for group in openings.values() for entry in group]
    sills = [rectangle[:, 2].min() for kind, rectangle in entries if kind == 'windows']
    band_top = min(sills) - .15 if sills else min(rectangle[:, 2].max() for _, rectangle in entries) - 1.35
    band_top = float(np.float32(band_top))
    for index, faces in groups.items():
        surface = FacadeSurface(facades[index], [triangle for triangle, _ in faces])
        for kind, triangle in _partition(surface, openings.get(index, ()), band_top):
            append(kind, triangle, surface.normal)
        for edge, points in surface.bottom_splits.items():
            splits.setdefault(edge, {}).update(points)
    for triangle, normal in refined_caps(caps, splits):
        append('walls', triangle, normal)
    return tuple(_indexed_mesh(triangles[kind], normals[kind]) for kind in triangles)
