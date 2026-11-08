"""Exterior landings, low side ramps and guarded balconies in ENU metres."""

from dataclasses import dataclass
from functools import lru_cache

import numpy as np
from PIL import Image, ImageDraw
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
# Match players, zombies and dogs. Include the landing's 1 cm walking offset.
MAX_STEP_HEIGHT = .30
HEIGHT_TOLERANCE = 1e-6
# FPSController's 1.10 m crouched body plus 3 cm below a 2 cm lintel.
BALCONY_CLEARANCE = 1.13
LOWER_DOOR_LINTEL = .02
RAILING_HEIGHT = 1.0
ENTRANCE_MATERIAL = PBRMaterial(name="building_entrance_grey", baseColorFactor=[140, 140, 140, 255],
                               doubleSided=True, metallicFactor=0.0, roughnessFactor=1.0)


@dataclass
class Entrance:
    center: np.ndarray
    direction: np.ndarray
    outward: np.ndarray
    floor: float
    lower_floor: float | None = None
    step_height: float | None = None


def classify_entrance(entrance, elevation):
    """Find a crouchable route beneath the landing using rendered terrain edges.

    The 1 m wide approach includes the doorway and the full landing depth.
    Use the highest ground on its cross sections, rather than a single low
    sample that could leave the lower doorway impassable.
    """
    profiles = []
    for out in (0, LANDING_DEPTH / 2, LANDING_DEPTH):
        center = entrance.center + entrance.outward * out
        profiles.append(LineString([center - entrance.direction * .5, center + entrance.direction * .5]))
    for along in (-.5, 0, .5):
        center = entrance.center + entrance.direction * along
        profiles.append(LineString([center, center + entrance.outward * LANDING_DEPTH]))
    ground = []
    door_ground = None
    for line in profiles:
        xy, _ = sample_terrain_line(line, elevation)
        heights = np.broadcast_to(elevation(*xy.T), (len(xy),))
        if door_ground is None:
            door_ground = float(np.max(heights))
        ground.extend(heights)
    if not np.isfinite(ground).all():
        raise ValueError("Non-finite terrain below an entrance landing")
    highest = float(np.max(ground))
    entrance.step_height = max(0., entrance.floor + .01 - float(np.min(ground)))
    if entrance.floor - LOWER_DOOR_LINTEL - highest >= BALCONY_CLEARANCE:
        entrance.lower_floor = float(np.float32(door_ground))
    return entrance


@lru_cache(maxsize=1)
def _railing_material():
    # One horizontal top rail, with vertical bars below; no lower cross rail.
    texture = Image.new("RGBA", (128, 256), (255, 255, 255, 0))
    draw = ImageDraw.Draw(texture)
    draw.rectangle((0, 0, 127, 9), fill="white")
    draw.rectangle((0, 0, 5, 255), fill="white")
    draw.rectangle((122, 0, 127, 255), fill="white")
    return PBRMaterial(name="building_balcony_railing", baseColorFactor=[75, 82, 78, 255],
                       baseColorTexture=texture, alphaMode="MASK", alphaCutoff=.5,
                       doubleSided=True, metallicFactor=.35, roughnessFactor=.85)


def build_entrance_railings(entrances):
    """Three continuous collision quads with alpha-masked top rail and bars."""
    vertices, faces, uv = [], [], []
    half = LANDING_WIDTH / 2
    for entrance in entrances:
        if entrance.lower_floor is None:
            continue
        corners = [(-half, 0), (-half, LANDING_DEPTH), (half, LANDING_DEPTH), (half, 0)]
        for a, b in zip(corners[:-1], corners[1:]):
            start = len(vertices)
            for along, out in (a, b, b, a):
                xy = entrance.center + entrance.direction * along + entrance.outward * out
                vertices.append([*xy, entrance.floor + .01 + (RAILING_HEIGHT if len(vertices) - start >= 2 else 0)])
            repeat = float(np.linalg.norm(np.subtract(b, a))) / .20
            uv.extend([(0, 0), (repeat, 0), (repeat, 1), (0, 1)])
            faces.extend([[start, start + 1, start + 2], [start, start + 2, start + 3]])
    if not faces:
        return None
    return trimesh.Trimesh(vertices=np.asarray(vertices, dtype=np.float32), faces=faces, process=False,
                           visual=trimesh.visual.TextureVisuals(uv=uv, material=_railing_material()))


def build_entrances(entrances, elevation):
    """Return walkable sheets for classified entrances in ENU metres.

    Steps at most 30 cm high keep just the landing. Higher ramps descend along
    the facade on both sides, with a fixed 2 m horizontal
    run. Their ends may be buried. The landing aligns with runtime floors,
    which sit 1 cm above the facade's floor datum. The exposed front of each
    ramp has a triangular cheek down to its low endpoint; the landing keeps
    its separate, conditional front skirt. Balconies keep only the landing;
    build_entrance_railings supplies their separate masked guard material.
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
        if entrance.step_height is None:
            classify_entrance(entrance, elevation)
        def point(along, out, height):
            xy = entrance.center + entrance.direction * along + entrance.outward * out
            return np.array([*xy, height])

        half = LANDING_WIDTH / 2
        top = entrance.floor + .01
        # Facade direction cross outward points down for a CCW exterior.
        quad([point(-half, 0, top), point(-half, LANDING_DEPTH, top),
              point(half, LANDING_DEPTH, top), point(half, 0, top)])
        needs_ramps = entrance.lower_floor is None and entrance.step_height > MAX_STEP_HEIGHT + HEIGHT_TOLERANCE
        for side in ((-1, 1) if needs_ramps else ()):
            inner, outer = side * half, side * (half + RAMP_RUN)
            low = top - RAMP_RUN * RAMP_SLOPE
            points = [point(inner, 0, top), point(outer, 0, low),
                      point(outer, LANDING_DEPTH, low), point(inner, LANDING_DEPTH, top)]
            quad(points if side < 0 else points[::-1])
            cheek = [point(inner, LANDING_DEPTH, top), point(inner, LANDING_DEPTH, low),
                     point(outer, LANDING_DEPTH, low)]
            triangle(cheek if side > 0 else cheek[::-1])

        if entrance.lower_floor is not None:
            continue
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
