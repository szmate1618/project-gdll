"""Terrain-following OSM fences in local east/north/up metres.

Panels are batched by material, with posts painted into their textures. Wire
and railing panels use embedded alpha masks; their continuous triangle surfaces
also block walking. Mapped entrances are gaps, without interactive gate leaves.
"""
from __future__ import annotations

from collections import Counter, defaultdict
import logging
import math

import numpy as np
from PIL import Image, ImageDraw
from shapely.geometry import box
import trimesh
from trimesh.visual.material import PBRMaterial
from trimesh.visual.texture import TextureVisuals

from .roads import parse_width
from .terrain import Terrain
from .terrain_surfaces import geometry_parts

LOG = logging.getLogger(__name__)
COLORS = {
    "chain_link": [150, 155, 150, 255], "metal": [75, 82, 78, 255],
    "wood": [140, 104, 65, 255], "concrete": [165, 160, 150, 255],
}


def fence_style(tags: dict) -> str:
    """Resolve a reusable approximation from construction and material tags."""
    material = str(tags.get("material", "")).lower().split(";")[0].strip()
    kind = str(tags.get("fence_type", "")).lower()
    if material in {"wood", "timber"} or kind in {"wood", "solid_wood"}:
        return "wood"
    if material in {"concrete", "brick", "stone"} or kind in {"concrete", "brick", "stone"}:
        return "concrete"
    if kind in {"railing", "metal", "bars", "wrought_iron"}:
        return "metal"
    return "chain_link"


def _material(style: str, spacing: float) -> PBRMaterial:
    # U spans one post interval. Keep the wire/bar pattern at .25 m within
    # that interval, rather than placing a post at every wire repeat.
    width = min(4096, 2 ** math.ceil(math.log2(max(128, spacing * 512))))
    masked = style in {"chain_link", "metal"}
    texture = Image.new("RGBA", (width, 128), (255, 255, 255, 0 if masked else 255))
    draw = ImageDraw.Draw(texture)
    cell_width = width * .25 / spacing
    if style == "chain_link":
        for cell in range(-1, math.ceil(width / cell_width) + 1):
            offset = cell * cell_width
            draw.line([(offset, 0), (offset + cell_width, 128)], fill="white", width=5)
            draw.line([(offset, 128), (offset + cell_width, 0)], fill="white", width=5)
    elif style == "metal":
        bar_width = width * .02 / spacing
        for cell in range(math.ceil(width / cell_width)):
            center = (cell + .5) * cell_width
            draw.rectangle((center - bar_width / 2, 0, center + bar_width / 2, 127), fill="white")
        draw.rectangle((0, 12, width - 1, 18), fill="white")
        draw.rectangle((0, 104, width - 1, 110), fill="white")
    # An 8 cm post straddles the repeat seam. Shading suggests its thickness
    # while every fence vertex remains on the continuous panel surface.
    half_post = max(1, round(width * .04 / spacing))
    draw.rectangle((0, 0, half_post - 1, 127), fill=(235, 235, 235, 255))
    draw.rectangle((width - half_post, 0, width - 1, 127), fill=(175, 175, 175, 255))
    return PBRMaterial(
        name=f"fence_{style}",
        baseColorFactor=COLORS[style], baseColorTexture=texture,
        metallicFactor=.35 if style in {"chain_link", "metal"} else 0,
        roughnessFactor=.85, doubleSided=True,
        alphaMode="MASK" if masked else "OPAQUE", alphaCutoff=.5,
    )


def _sample_line(line, elevation, spacing):
    """Retain OSM corners and split panels at rendered terrain triangle edges."""
    coords = np.asarray(line.coords, dtype=float)
    xy, distances = [coords[0]], [0.0]
    distance = 0.0
    for a, b in zip(coords[:-1], coords[1:]):
        length = float(np.linalg.norm(b - a))
        if length < 1e-7:
            continue
        fractions = list(np.linspace(0, 1, math.ceil(length / spacing) + 1))
        if isinstance(elevation, Terrain):
            # Terrain faces share integer grid edges and NW-to-SE diagonals,
            # which lie on integer values of (row - column).
            grid_a = np.array([(a[0] - elevation.bounds[0]) / elevation.dx,
                               (elevation.bounds[3] - a[1]) / elevation.dy])
            grid_b = np.array([(b[0] - elevation.bounds[0]) / elevation.dx,
                               (elevation.bounds[3] - b[1]) / elevation.dy])
            for start, end in zip([*grid_a, grid_a[1] - grid_a[0]],
                                  [*grid_b, grid_b[1] - grid_b[0]]):
                if abs(end - start) < 1e-12:
                    continue
                for edge in range(math.floor(min(start, end)) + 1, math.ceil(max(start, end))):
                    fractions.append((edge - start) / (end - start))
        previous = 0.0
        for fraction in sorted(fractions):
            if (fraction - previous) * length < 1e-5:
                continue
            xy.append(a + fraction * (b - a))
            distances.append(distance + fraction * length)
            previous = fraction
        distance += length
    return np.asarray(xy), np.asarray(distances)


def _panels(line, elevation, spacing, height, style, material):
    xy, distances = _sample_line(line, elevation, spacing)
    ground = np.broadcast_to(elevation(*xy.T), (len(xy),))
    if not np.isfinite(ground).all():
        raise ValueError("Terrain returned non-finite fence elevations")
    vertices = np.repeat(np.column_stack((xy, ground)), 2, axis=0)
    vertices[1::2, 2] += height
    # A small buried skirt prevents float32 seams exposing a gap at ground.
    vertices[::2, 2] -= .05
    lower = np.arange(len(xy) - 1) * 2
    faces = np.vstack((np.column_stack((lower, lower + 2, lower + 3)),
                       np.column_stack((lower, lower + 3, lower + 1))))
    uv = np.column_stack((np.repeat(distances / spacing, 2),
                          np.tile([-.05 / .25, height / .25] if style == "chain_link"
                                  else [-.05 / height, 1], len(xy))))
    return trimesh.Trimesh(vertices=vertices, faces=faces, process=False,
                           visual=TextureVisuals(uv=uv, material=material))


def add_fences(scene: trimesh.Scene, features: list, elevation, config: dict) -> dict:
    """Build fences from local metric ways, cutting entrances before clipping.

    Heights default to 1.5 m; textured posts to 2.5 m spacing; gate widths to 3 m.
    Narrow pedestrian entrances default to 1.2 m. No fences are inferred from
    land-use or property boundaries that lack an explicit barrier=fence tag.
    """
    settings = config.get("fences", {})
    default_height = float(settings.get("default_height_m", 1.5))
    spacing = float(settings.get("post_spacing_m", 2.5))
    gate_width = float(settings.get("default_gate_width_m", 3))
    for name, value in [("default_height_m", default_height), ("post_spacing_m", spacing),
                        ("default_gate_width_m", gate_width)]:
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"fences.{name} must be positive and finite")
    clip = config.get("_clip_polygon")
    if clip is None and config.get("_clip_bounds") is not None:
        clip = box(*config["_clip_bounds"])
    groups, sources = defaultdict(list), defaultdict(set)
    materials, openings = Counter(), set()
    material_cache = {}
    ways = segments = skipped = 0
    length = 0.0
    for feature in features:
        tags = feature.get("tags", {})
        geometry = feature.get("geometry")
        if tags.get("barrier") != "fence" or geometry is None:
            skipped += 1
            continue
        style = fence_style(tags)
        height = parse_width(tags.get("height")) or default_height
        for opening in feature.get("openings", []):
            point = opening["geometry"]
            fallback = 1.2 if opening["tags"].get("barrier") in {"stile", "kissing_gate"} else gate_width
            width = parse_width(opening["tags"].get("width")) or fallback
            cut = point.buffer(width / 2, quad_segs=16)
            if geometry.intersects(cut) and (clip is None or clip.intersects(cut)):
                geometry = geometry.difference(cut)
                openings.add(opening["id"])
        if clip is not None:
            geometry = geometry.intersection(clip)
        accepted = False
        for line in geometry_parts(geometry, "LineString"):
            if line.length < .05:
                continue
            if style not in material_cache:
                material_cache[style] = _material(style, spacing)
            mesh = _panels(line, elevation, spacing, height, style, material_cache[style])
            groups[style].append(mesh)
            sources[style].add(str(feature.get("id", "unknown")))
            segments += 1
            materials[style] += 1
            length += line.length
            accepted = True
        ways += accepted
        skipped += not accepted
    triangles = 0
    for key, meshes in sorted(groups.items()):
        mesh = trimesh.util.concatenate(meshes)
        mesh.metadata.update(kind="fence", source="OpenStreetMap", osm_ids=sorted(sources[key]))
        scene.add_geometry(mesh, geom_name=f"fences_{key}", node_name=f"fences_{key}")
        triangles += len(mesh.faces)
    LOG.info("Created %d fence ways (%.1f m) with %d entrance gaps", ways, length, len(openings))
    return {"fence_ways": ways, "fence_segments": segments, "fence_meshes": len(groups),
            "fence_triangles": triangles, "fence_length_m": round(length, 3),
            "fence_materials": dict(materials), "fence_openings": len(openings),
            "fences_skipped": skipped}
