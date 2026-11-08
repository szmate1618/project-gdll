"""Construct legacy basement-cut terrain for reader compatibility tests."""

import numpy as np
from shapely import constrained_delaunay_triangles, polygons, union_all
from shapely.strtree import STRtree
import trimesh


def carve_basement_terrain(scene, footprints):
    """Clip ENU terrain triangles once, interpolating height, UVs and normals.

    Basement walls and their bottom caps belong to the building generator.
    Only terrain inside those closed footprints is removed here. A spatial
    query limits polygon clipping to triangles that intersect a basement.
    """
    if not footprints:
        return
    tree = STRtree(footprints)
    for name, mesh in list(scene.geometry.items()):
        if name != "terrain":
            continue
        triangles = mesh.triangles
        projected = polygons(triangles[:, :, :2])
        candidates = tree.query(projected, predicate="intersects")
        affected = np.unique(candidates[0])
        if not len(affected):
            continue
        keep = np.ones(len(mesh.faces), dtype=bool)
        keep[affected] = False
        vertices, uv, normals, faces = [], [], [], []
        for index in affected:
            cutters = [footprints[i] for i in tree.query(projected[index], predicate="intersects")]
            remaining = projected[index].difference(union_all(cutters))
            if remaining.is_empty:
                continue
            original = triangles[index]
            basis = (original[1:, :2] - original[0, :2]).T
            old_uv = mesh.visual.uv[mesh.faces[index]]
            old_normals = mesh.vertex_normals[mesh.faces[index]]
            for triangle in constrained_delaunay_triangles(remaining).geoms:
                xy = np.asarray(triangle.exterior.coords)[:3]
                weights = np.linalg.solve(basis, (xy - original[0, :2]).T).T
                weights = np.column_stack([1 - weights.sum(axis=1), weights])
                points = weights @ original
                if np.cross(points[1] - points[0], points[2] - points[0])[2] < 0:
                    weights = weights[::-1]
                    points = points[::-1]
                points = points.astype(np.float32).astype(float)
                cross = np.cross(points[1] - points[0], points[2] - points[0])
                if abs(cross[2]) < 2e-10:
                    continue
                if cross[2] < 0:
                    weights = weights[::-1]
                    points = points[::-1]
                start = len(mesh.vertices) + len(vertices)
                vertices.extend(points)
                uv.extend(weights @ old_uv)
                normals.extend(weights @ old_normals)
                faces.append([start, start + 1, start + 2])
        replacement = trimesh.Trimesh(
            vertices=np.vstack([mesh.vertices, np.asarray(vertices).reshape(-1, 3)]),
            faces=np.vstack([mesh.faces[keep], np.asarray(faces, dtype=np.int64).reshape(-1, 3)]),
            vertex_normals=np.vstack([mesh.vertex_normals, np.asarray(normals).reshape(-1, 3)]), process=False,
            visual=trimesh.visual.TextureVisuals(
                uv=np.vstack([mesh.visual.uv, np.asarray(uv).reshape(-1, 2)]), material=mesh.visual.material))
        replacement.metadata.update(mesh.metadata)
        scene.geometry[name] = replacement
