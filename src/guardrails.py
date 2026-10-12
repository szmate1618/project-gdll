"""Minimal double-sided roadside strips with rail and posts in one alpha texture."""
from __future__ import annotations

from collections import defaultdict
import math

import numpy as np
from PIL import Image, ImageDraw
import trimesh
from trimesh.visual.material import PBRMaterial
from trimesh.visual.texture import TextureVisuals

from .buildings import parse_length
from .config import ROOT
from .terrain_profile import sample_terrain_line, profile_corners

TEXTURE_PATH = ROOT / "assets/guardrails/guardrail.png"
POST_SPACING_M = 2.0
BATCH_SIZE_M = 128.0


def create_guardrail_texture():
    """One two-meter repeat: a shaded grey beam and an 8 cm post at the seam."""
    image = Image.new("RGBA", (512, 128), (255, 255, 255, 0))
    draw = ImageDraw.Draw(image)
    # Image rows run top to bottom. Beam spans approximately 0.45–0.75 m.
    for row in range(51):
        shade = round(155 + 65 * abs(math.cos(row / 50 * math.pi * 2)))
        draw.line((0, row, 511, row), fill=(shade, shade, shade, 255))
    half_post = round(512 * .08 / POST_SPACING_M / 2)
    for left, right in ((0, half_post - 1), (512 - half_post, 511)):
        draw.rectangle((left, 0, right, 127), fill=(145, 145, 145, 255))
        draw.line((left, 0, left, 127), fill=(205, 205, 205, 255))
    return image


def _cell_spans(a, b):
    """Split a straight 3D profile only at horizontal batching boundaries."""
    fractions = [0., 1.]
    for axis in (0, 1):
        delta = b[axis] - a[axis]
        if abs(delta) < 1e-10:
            continue
        low, high = sorted((a[axis], b[axis]))
        for boundary in range(math.floor(low / BATCH_SIZE_M) + 1, math.ceil(high / BATCH_SIZE_M)):
            fraction = (boundary * BATCH_SIZE_M - a[axis]) / delta
            if 1e-10 < fraction < 1 - 1e-10:
                fractions.append(fraction)
    fractions = sorted(set(fractions))
    for start, end in zip(fractions[:-1], fractions[1:]):
        if end - start < 1e-10:
            continue
        middle = a + (b - a) * ((start + end) * .5)
        key = tuple(math.floor(value / BATCH_SIZE_M) for value in middle[:2])
        yield key, start, end


def add_guardrails(scene, features, terrain):
    """Bake local east/north/up strips; two triangles per bend/slope/cell span.

    The entire strip blocks walking and sight, including its transparent area.
    Geometry follows the published terrain's triangles. Posts add no vertices.
    """
    with Image.open(TEXTURE_PATH) as image:
        material = PBRMaterial(name="guardrail_metal", baseColorFactor=[255, 255, 255, 255],
                               baseColorTexture=image.convert("RGBA"), metallicFactor=.4,
                               roughnessFactor=.8, doubleSided=True, alphaMode="MASK", alphaCutoff=.5)
    groups = defaultdict(list)
    sources = defaultdict(set)
    way_ids = set()
    length = 0.
    for feature in features:
        line = feature["geometry"]
        if line.length < .05:
            continue
        height = parse_length(feature["tags"].get("height"))
        if height is None or not .3 <= height <= 2:
            height = .75
        xy, distances = sample_terrain_line(line, terrain)
        if len(xy) < 2:
            continue
        ground = np.broadcast_to(terrain(*xy.T), (len(xy),))
        if not np.isfinite(ground).all():
            raise ValueError("Nonfinite guardrail ground height")
        profile = np.column_stack((xy, ground))
        keep = profile_corners(profile)
        profile, distances = profile[keep], distances[keep]
        for index, (a, b) in enumerate(zip(profile[:-1], profile[1:])):
            for key, start, end in _cell_spans(a, b):
                points = np.array([a + (b - a) * start, a + (b - a) * end])
                vertices = np.repeat(points, 2, axis=0)
                vertices[::2, 2] -= .03
                vertices[1::2, 2] += height
                distances_at_ends = distances[index] + (distances[index + 1] - distances[index]) * np.array([start, end])
                uv = np.column_stack((np.repeat(distances_at_ends / POST_SPACING_M, 2), np.tile([0., 1.], 2)))
                mesh = trimesh.Trimesh(vertices=vertices, faces=[[0, 2, 3], [0, 3, 1]],
                                       process=False, visual=TextureVisuals(uv=uv, material=material))
                groups[key].append(mesh)
                sources[key].add(feature["id"])
        way_ids.add(feature["id"].split(":")[0])
        length += line.length
    triangles = 0
    for key, meshes in sorted(groups.items()):
        mesh = trimesh.util.concatenate(meshes)
        mesh.metadata.update(kind="guardrail", osm_ids=sorted(sources[key]))
        name = f"guardrails_{key[0]}_{key[1]}"
        scene.add_geometry(mesh, geom_name=name, node_name=name)
        triangles += len(mesh.faces)
    return {"ways": len(way_ids), "length_m": length, "batches": len(groups), "triangles": triangles}
