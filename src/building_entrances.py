"""Exterior landing and side-ramp surfaces in local east/north/up metres."""

from dataclasses import dataclass

import numpy as np
from shapely.geometry import LineString
import trimesh
from trimesh.visual.material import PBRMaterial

from .terrain_profile import sample_terrain_line


LANDING_WIDTH = 1.5
LANDING_DEPTH = 1.2
RAMP_RUN = 2.0
# Match the preferred run/rise of interior stairs (house_layout_rules.cpp).
RAMP_SLOPE = 1.0 / 1.75
FRONT_CLOSURE_HEIGHT = .4
ENTRANCE_MATERIAL = PBRMaterial(name="building_entrance_grey", baseColorFactor=[140, 140, 140, 255],
                               doubleSided=True, metallicFactor=0.0, roughnessFactor=1.0)


@dataclass
class Entrance:
    center: np.ndarray
    direction: np.ndarray
    outward: np.ndarray
    floor: float


def build_entrances(entrances, elevation):
    """Return walkable sheets, leaving the space below high landings open.

    Ramps descend along the facade on both sides, with a fixed 2 m horizontal
    run. Their ends may be buried. The landing aligns with runtime floors,
    which sit 1 cm above the facade's floor datum. The exposed front of each
    ramp has a triangular cheek down to its low endpoint; the landing keeps
    its separate, conditional front skirt.
    """
    vertices, faces = [], []

    def triangle(points):
        start = len(vertices)
        vertices.extend(points)
        faces.append([start, start + 1, start + 2])

    def quad(points):
        start = len(vertices)
        vertices.extend(points)
        faces.extend([[start, start + 1, start + 2], [start, start + 2, start + 3]])

    for entrance in entrances:
        def point(along, out, height):
            xy = entrance.center + entrance.direction * along + entrance.outward * out
            return np.array([*xy, height])

        half = LANDING_WIDTH / 2
        top = entrance.floor + .01
        # Facade direction cross outward points down for a CCW exterior.
        quad([point(-half, 0, top), point(-half, LANDING_DEPTH, top),
              point(half, LANDING_DEPTH, top), point(half, 0, top)])
        for side in (-1, 1):
            inner, outer = side * half, side * (half + RAMP_RUN)
            low = top - RAMP_RUN * RAMP_SLOPE
            points = [point(inner, 0, top), point(outer, 0, low),
                      point(outer, LANDING_DEPTH, low), point(inner, LANDING_DEPTH, top)]
            quad(points if side < 0 else points[::-1])
            cheek = [point(inner, LANDING_DEPTH, top), point(inner, LANDING_DEPTH, low),
                     point(outer, LANDING_DEPTH, low)]
            triangle(cheek if side > 0 else cheek[::-1])

        # Sample across the whole front. A high point must not hide a crawlable
        # gap at the other end. Keep tall fronts entirely open.
        along = np.linspace(-half, half, 4)
        front = np.array([point(value, LANDING_DEPTH, top) for value in along])
        xy, _ = sample_terrain_line(LineString(front[:, :2]), elevation)
        front = np.column_stack([xy, np.full(len(xy), top)])
        ground = np.asarray(elevation(front[:, 0], front[:, 1]), dtype=float)
        if not np.isfinite(ground).all():
            raise ValueError("Non-finite terrain below an entrance landing")
        if np.max(top - ground) < FRONT_CLOSURE_HEIGHT:
            for a, b, ga, gb in zip(front[:-1], front[1:], ground[:-1], ground[1:]):
                # The 8 cm skirt below terrain prevents visible foundation gaps.
                bottom_a, bottom_b = a.copy(), b.copy()
                bottom_a[2], bottom_b[2] = min(ga - .08, top - .001), min(gb - .08, top - .001)
                quad([bottom_a, bottom_b, b, a])

    if not faces:
        return None
    mesh = trimesh.Trimesh(vertices=np.asarray(vertices, dtype=np.float32), faces=faces, process=False)
    mesh.visual = trimesh.visual.TextureVisuals(material=ENTRANCE_MATERIAL)
    return mesh
