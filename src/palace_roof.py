"""Photo-derived palace roof massing, independent of the interior layout.

The continuous pitched surface follows the existing footprint and eave datum.
Cupolas, drums and roof furniture are closed meshes above it. Dimensions are
inferred in the same facade/garden metre axes as the authored floorplan.
"""
from __future__ import annotations

from functools import lru_cache
import json
from pathlib import Path

import numpy as np
from shapely import constrained_delaunay_triangles, make_valid, points, distance
from shapely.geometry import Polygon
import trimesh
from trimesh.visual.material import PBRMaterial

ROOF_PATH = Path(__file__).resolve().parents[1] / "assets/buildings/godollo-palace/roof.json"
TILES = PBRMaterial(name="palace_terracotta_roof", baseColorFactor=[153, 77, 51, 255], roughnessFactor=.9)
METAL = PBRMaterial(name="palace_roof_metal", baseColorFactor=[69, 76, 72, 255], metallicFactor=.35, roughnessFactor=.7)
PLASTER = PBRMaterial(name="palace_roof_plaster", baseColorFactor=[224, 213, 193, 255], roughnessFactor=.92)
GLASS = PBRMaterial(name="palace_dormer_glass", baseColorFactor=[53, 76, 89, 255], roughnessFactor=.3)


@lru_cache(maxsize=1)
def roof_definition():
    return json.loads(ROOF_PATH.read_text(encoding="utf-8"))


def hip_height(points):
    """Upper envelope of separate wing hips, metres above the fixed eaves."""
    points = np.asarray(points, dtype=float)
    height = np.zeros(points.shape[:-1])
    for hip in roof_definition()["hips"]:
        angle = np.radians(hip["angle_degrees"])
        u = np.array([np.cos(angle), np.sin(angle)])
        v = np.array([-u[1], u[0]])
        local = (points - hip["center"]) @ np.column_stack([u, v])
        half_length, half_width = np.array(hip["size"]) / 2
        rise = np.minimum(half_width - np.abs(local[..., 1]), half_length - np.abs(local[..., 0]))
        height = np.maximum(height, np.clip(rise / half_width, 0, 1) * hip["rise_m"])
    return height


def _polygons(geometry):
    if isinstance(geometry, Polygon):
        if geometry.area > 1e-9:
            yield geometry
    else:
        for part in getattr(geometry, "geoms", ()):
            yield from _polygons(part)


def roof_surface(polygon, eaves, frame):
    """Triangulate an eave-sealed wing roof at exported float32 precision.

    A small cell grid resolves the hips and valleys without forcing one ridge
    across the whole zigzag footprint. Each clipped cell retains the real shell
    boundary. Quantize before triangulating, just like the generic roof builder.
    """
    origin, u, v, _ = frame
    basis = np.column_stack([u, v])
    local = (np.array(polygon.exterior.coords) - origin) @ basis
    low, high = local.min(axis=0), local.max(axis=0)
    spacing = roof_definition()["surface_grid_m"]
    xs = np.arange(np.floor(low[0] / spacing) * spacing, high[0] + spacing, spacing)
    ys = np.arange(np.floor(low[1] / spacing) * spacing, high[1] + spacing, spacing)
    # Keep principal ridges exactly straight rather than letting a grid cell
    # straddle their apex. Remaining valley intersections are approximated.
    xs = np.unique(np.r_[xs, [h["center"][0] for h in roof_definition()["hips"]]])
    ys = np.unique(np.r_[ys, [h["center"][1] for h in roof_definition()["hips"]]])
    triangles = []
    for x0, x1 in zip(xs[:-1], xs[1:]):
        if x1 <= low[0] or x0 >= high[0]:
            continue
        for y0, y1 in zip(ys[:-1], ys[1:]):
            if y1 <= low[1] or y0 >= high[1]:
                continue
            cell = np.array([[x0, y0], [x1, y0], [x1, y1], [x0, y1]]) @ basis.T + origin
            for piece in _polygons(polygon.intersection(Polygon(cell))):
                shell = np.asarray(piece.exterior.coords).astype(np.float32).astype(float)
                holes = [np.asarray(r.coords).astype(np.float32).astype(float) for r in piece.interiors]
                for fixed in _polygons(make_valid(Polygon(shell, holes))):
                    for triangle in constrained_delaunay_triangles(fixed).geoms:
                        triangles.append(np.asarray(triangle.exterior.coords)[:3])
    xy, inverse = np.unique(np.array(triangles).reshape(-1, 2), axis=0, return_inverse=True)
    plan_points = (xy - origin) @ basis
    rise = hip_height(plan_points)
    # A metre-scale source trace does not exactly match every skewed OSM edge.
    # Feather that mismatch down to the real eaves rather than raising the
    # exterior walls or punching gaps below a roof that overhangs the footprint.
    boundary_distance = distance(points(xy), polygon.boundary)
    rise = np.minimum(rise, boundary_distance * 1.05)
    # Export rounding moves a boundary sample by a few tens of micrometres.
    # Keep those samples at the exact shared eave datum, not a tiny raised lip.
    rise[boundary_distance < .001] = 0
    vertices = np.column_stack([xy, eaves + rise]).astype(np.float32).astype(float)
    faces = inverse.reshape(-1, 3)
    cross = np.cross(vertices[faces[:, 1]] - vertices[faces[:, 0]], vertices[faces[:, 2]] - vertices[faces[:, 0]])
    faces[cross[:, 2] < 0] = faces[cross[:, 2] < 0, ::-1]
    roof = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    roof.update_faces(roof.unique_faces())
    roof.update_faces(roof.nondegenerate_faces(height=1e-8))
    roof.remove_unreferenced_vertices()
    roof.visual = trimesh.visual.TextureVisuals(material=TILES)
    return roof


def _loft(center, radius, profile, segments=32, exponent=1):
    """Closed elliptical rings with explicit end caps and one apex vertex."""
    angles = np.arange(segments) * 2 * np.pi / segments
    circle = np.column_stack([np.cos(angles), np.sin(angles)])
    circle = np.sign(circle) * np.abs(circle) ** exponent
    vertices, faces, rings = [], [], []
    for height, scale in profile:
        ring = list(range(len(vertices), len(vertices) + segments)) if scale else [len(vertices)]
        if scale:
            vertices.extend([*p, height] for p in circle * np.array(radius) * scale + center)
        else:
            vertices.append([*center, height])
        rings.append(ring)
    # Counterclockwise rings are seen from above. The lower cap faces down.
    for i in range(1, len(rings[0]) - 1):
        faces.append([rings[0][0], rings[0][i + 1], rings[0][i]])
    for lower, upper in zip(rings[:-1], rings[1:]):
        for i, a in enumerate(lower):
            b = lower[(i + 1) % len(lower)]
            if len(upper) == 1:
                faces.append([a, b, upper[0]])
            else:
                c, d = upper[(i + 1) % len(upper)], upper[i]
                faces.extend([[a, b, c], [a, c, d]])
    for i in range(1, len(rings[-1]) - 1):
        faces.append([rings[-1][0], rings[-1][i], rings[-1][i + 1]])
    return trimesh.Trimesh(vertices=vertices, faces=faces, process=False)


def roof_details(eaves, frame):
    """Return shared-material batches of closed cupolas, dormers and chimneys."""
    origin, u, v, _ = frame
    batches = {"tiles": [], "metal": [], "plaster": [], "glass": []}
    for cupola in roof_definition()["cupolas"]:
        center, radius = np.array(cupola["center"]), np.array(cupola["radius"])
        bottom = cupola["drum_base_m"]
        shoulder = bottom + cupola["drum_height_m"]
        height = cupola["dome_height_m"]
        exponent = 1 if cupola["metal"] else .6
        batches["plaster"].append(_loft(center, radius, [(bottom, 1), (shoulder, 1)], exponent=exponent))
        profile = [(shoulder + t * height, scale) for t, scale in
                   [(0, 1), (.18, 1.025), (.42, .96), (.68, .80), (.86, .58), (.96, .34), (1, .16)]]
        batches["metal" if cupola["metal"] else "tiles"].append(_loft(center, radius, profile, exponent=exponent))
        top = shoulder + height
        lantern = cupola["lantern_height_m"]
        batches["metal"].append(_loft(center, radius * .14,
            [(top, 1.2), (top + lantern * .15, 1), (top + lantern * .75, .8), (top + lantern, .5)], 12))
        finial = cupola["finial_height_m"]
        batches["metal"].append(_loft(center, [.18, .18],
            [(top + lantern, 1), (top + lantern + finial * .25, 1.4),
             (top + lantern + finial * .45, .4), (top + lantern + finial, 0)], 12))
        # Raised meridian seams identify the tile-covered Baroque cupolas.
        if not cupola["metal"]:
            for angle in np.arange(8) * np.pi / 4:
                direction = np.array([np.cos(angle), np.sin(angle)])
                direction = np.sign(direction) * np.abs(direction) ** exponent
                points = [np.r_[center + radius * scale * direction, z] for z, scale in profile]
                for a, b in zip(points[:-1], points[1:]):
                    batches["metal"].append(trimesh.creation.cylinder(radius=.045, segment=[a, b], sections=6))
    for x, y in roof_definition()["chimneys"]:
        base = float(hip_height([x, y]))
        box = trimesh.creation.box([.65, .8, 1.4]); box.apply_translation([x, y, base + .6])
        batches["plaster"].append(box)
        cap = trimesh.creation.box([.85, 1, .16]); cap.apply_translation([x, y, base + 1.38])
        batches["metal"].append(cap)
    for x, y, angle in roof_definition()["dormers"]:
        base = float(hip_height([x, y]))
        # A small gabled housing, with a dark pane on its exposed front face.
        vertices = [[-.65, -.5, 0], [.65, -.5, 0], [.65, .8, 0], [-.65, .8, 0],
                    [-.65, -.5, 1.05], [.65, -.5, 1.05], [.65, .8, 1.05], [-.65, .8, 1.05],
                    [0, -.5, 1.65], [0, .8, 1.65]]
        faces = [[0, 2, 1], [0, 3, 2], [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5],
                 [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7], [4, 5, 8], [6, 7, 9],
                 [4, 8, 9], [4, 9, 7], [8, 5, 6], [8, 6, 9]]
        housing = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
        housing.fix_normals()
        rotation = trimesh.transformations.rotation_matrix(np.radians(angle), [0, 0, 1])
        housing.apply_transform(rotation); housing.apply_translation([x, y, base - .15])
        batches["plaster"].append(housing)
        pane = trimesh.creation.box([.7, .025, .75])
        pane.apply_translation([0, -.52, .65]); pane.apply_transform(rotation); pane.apply_translation([x, y, base - .15])
        batches["glass"].append(pane)
    transform = np.eye(4)
    transform[:2, 0] = u; transform[:2, 1] = v; transform[:2, 3] = origin
    transform[2, 3] = eaves
    materials = {"tiles": TILES, "metal": METAL, "plaster": PLASTER, "glass": GLASS}
    result = {}
    for name, meshes in batches.items():
        if not meshes:
            continue
        mesh = trimesh.util.concatenate(meshes)
        mesh.apply_transform(transform)
        mesh.vertices = mesh.vertices.astype(np.float32).astype(float)
        mesh.visual = trimesh.visual.TextureVisuals(material=materials[name])
        result[name] = mesh
    return result
