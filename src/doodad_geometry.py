"""Original low-poly ground-detail meshes, in Y-up meters.

All models rest on Y=0. Material boundaries share positions; weld identical
coordinates to inspect the closed solid. No external assets or random state
are involved in authoring these three fixed shapes.
"""
from __future__ import annotations

import math


COPYRIGHT = "Original project asset, CC0 1.0"


def _part(name, positions, triangles, color, roughness=1.0):
    """Keep each material part's indices local and omit unused vertices."""
    used = sorted({index for face in triangles for index in face})
    remap = {old: new for new, old in enumerate(used)}
    return {
        "name": name,
        "positions": [list(positions[index]) for index in used],
        "triangles": [[remap[index] for index in face] for face in triangles],
        "color": list(color),
        "roughness": roughness,
    }


def _normal(positions, triangle):
    a, b, c = (positions[index] for index in triangle)
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    return (u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0])


def branch_model():
    """A nine-corner Y footprint with three triangular-section limbs.

    Three end ridges meet at one fork ridge. This produces a single solid,
    rather than overlapping triangular prisms with hidden internal faces.
    The right fork is substantially shorter than the left fork.
    """
    positions = [
        (-0.034, 0.0, 0.400),  # 0..8: clockwise XZ outline, viewed from above.
        (0.034, 0.0, 0.400),
        (0.045, 0.0, 0.025),
        (0.224, 0.0, -0.145),
        (0.193, 0.0, -0.180),
        (0.000, 0.0, -0.025),
        (-0.248, 0.0, -0.390),
        (-0.286, 0.0, -0.366),
        (-0.047, 0.0, 0.020),
        (0.000, 0.033, 0.010),  # 9: shared fork ridge.
        # 10..12: intersect the two roof planes through the fork ridge and
        # each limb's bottom side edges with its vertical cut-end plane.
        # Keeping these intersections precise makes both slopes planar, so
        # intermediate cross sections remain triangles without extra kinks.
        (0.0006970264229717602, 0.02418569824035942, 0.400),
        (0.20523703335287571, 0.022292607547360548, -0.166183994601592),
        (-0.2595222517744908, 0.022850486491149274, -0.38272278835295315),
    ]
    # Each limb has two flat roof slopes. Longitudinal cross sections are
    # triangles with an edge on the bottom and a point on the ridge.
    roof = [
        (0, 8, 9), (0, 9, 10),
        (1, 10, 9), (1, 9, 2),
        (8, 7, 12), (8, 12, 9),
        (6, 5, 9), (6, 9, 12),
        (2, 3, 11), (2, 11, 9),
        (4, 5, 9), (4, 9, 11),
    ]
    roof = [face if _normal(positions, face)[1] > 0 else face[::-1]
            for face in roof]
    # Seven triangles cover the concave nonagon exactly: three limb quads
    # and the small triangle between their roots. Downward winding closes it.
    bottom = [(0, 2, 1), (0, 8, 2),
              (2, 4, 3), (2, 5, 4),
              (8, 6, 5), (8, 7, 6), (8, 5, 2)]
    cuts = [(0, 1, 10), (3, 4, 11), (6, 7, 12)]
    for index, face in enumerate(cuts):
        center = [sum(positions[v][axis] for v in face) / 3 for axis in range(3)]
        outward = [center[0] - positions[9][0], 0, center[2] - positions[9][2]]
        if sum(a * b for a, b in zip(_normal(positions, face), outward)) < 0:
            cuts[index] = face[::-1]
    return {
        "id": "branch",
        "copyright": COPYRIGHT,
        "metadata": {
            "description": "Asymmetric Y branch; concave nonagon underside and triangular limbs",
            "bottom_outline_vertices": 9,
            "cross_section_sides": 3,
        },
        "parts": [
            _part("Branch_Bark", positions, roof + bottom, (0.31, 0.16, 0.070)),
            _part("Branch_Cut_Ends", positions, cuts, (0.57, 0.35, 0.16)),
        ],
    }


def _ring_sides(count, lower, upper):
    triangles = []
    for side in range(count):
        following = (side + 1) % count
        triangles.extend([(lower + side, upper + side, upper + following),
                          (lower + side, upper + following, lower + following)])
    return triangles


def _ring_cap(count, offset, upward):
    triangles = [(offset, offset + side, offset + side + 1)
                 for side in range(1, count - 1)]
    return [face[::-1] for face in triangles] if upward else triangles


def stone_model():
    """One irregular pentagonal frustum with parallel, planar caps."""
    angles = (8, 81, 151, 226, 300)
    radii = (0.082, 0.074, 0.091, 0.076, 0.084)
    bottom = [(radius * math.cos(math.radians(angle)), 0.0,
               radius * math.sin(math.radians(angle)))
              for angle, radius in zip(angles, radii)]
    # Scaling the same irregular pentagon retains five planar frustum sides.
    top = [(0.55 * x + 0.008, 0.074, 0.55 * z - 0.006)
           for x, _, z in bottom]
    positions = bottom + top
    triangles = (_ring_sides(5, 0, 5)
                 + _ring_cap(5, 0, False) + _ring_cap(5, 5, True))
    return {
        "id": "stone",
        "copyright": COPYRIGHT,
        "metadata": {
            "description": "Irregular pentagonal frustum with a smaller offset top",
            "cross_section_sides": 5,
        },
        "parts": [_part("Stone", positions, triangles, (0.46, 0.47, 0.43))],
    }


def beer_bottle_model():
    """Exactly two hexagonal columns joined by one hexagonal shoulder."""
    levels = ((0.000, 0.0325), (0.155, 0.0325),
              (0.195, 0.0130), (0.250, 0.0130))
    positions = [(radius * math.cos(math.pi / 6 + side * math.pi / 3), y,
                  radius * math.sin(math.pi / 6 + side * math.pi / 3))
                 for y, radius in levels for side in range(6)]
    sides = [face for band in range(3) for face in _ring_sides(6, band * 6, (band + 1) * 6)]
    bottom = _ring_cap(6, 0, False)
    top = _ring_cap(6, 18, True)
    return {
        "id": "beer_bottle",
        "copyright": COPYRIGHT,
        "metadata": {
            "description": "Hexagonal body column, shoulder frustum and neck column",
            "cross_section_sides": 6,
            "height_m": 0.25,
            "diameter_m": 0.065,
            "column_count": 2,
            "frustum_count": 1,
        },
        "parts": [
            _part("Beer_Bottle_Amber", positions, sides + bottom,
                  (0.30, 0.12, 0.035), roughness=0.28),
            _part("Beer_Bottle_Mouth", positions, top,
                  (0.055, 0.025, 0.008), roughness=0.4),
        ],
    }


def models():
    return [branch_model(), stone_model(), beer_bottle_model()]
