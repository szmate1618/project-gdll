"""Terrain-following OSM fences in local east/north/up metres.

Panels and posts are batched by material. Wire and railing panels use embedded
alpha masks; their continuous triangle surfaces also block walking through the
fence. Mapped entrances are physical gaps, without interactive gate leaves.
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


def _material(style: str, posts: bool = False) -> PBRMaterial:
    texture = None
    if not posts and style in {"chain_link", "metal"}:
        # One repeat spans .25 horizontal metres. Railing V spans the entire
        # height; chain-link V repeats every .25 vertical metres.
        texture = Image.new("RGBA", (128, 128), (255, 255, 255, 0))
        draw = ImageDraw.Draw(texture)
        if style == "chain_link":
            for offset in (-128, 0, 128):
                draw.line([(offset, 0), (offset + 128, 128)], fill="white", width=5)
                draw.line([(offset, 128), (offset + 128, 0)], fill="white", width=5)
        else:
            draw.rectangle((59, 0, 69, 127), fill="white")
            draw.rectangle((0, 12, 127, 18), fill="white")
            draw.rectangle((0, 104, 127, 110), fill="white")
    return PBRMaterial(
        name=f"fence_{style}" + ("_posts" if posts else ""),
        baseColorFactor=COLORS[style], baseColorTexture=texture,
        metallicFactor=.35 if style in {"chain_link", "metal"} else 0,
        roughnessFactor=.85, doubleSided=True,
        alphaMode="MASK" if texture is not None else "OPAQUE", alphaCutoff=.5,
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
    uv = np.column_stack((np.repeat(distances / .25, 2),
                          np.tile([-.05 / .25, height / .25] if style == "chain_link"
                                  else [-.05 / height, 1], len(xy))))
    return trimesh.Trimesh(vertices=vertices, faces=faces, process=False,
                           visual=TextureVisuals(uv=uv, material=material))


def _posts(line, elevation, spacing, height, material):
    # Posts use regular spacing and original corners, independently of extra
    # terrain panel subdivisions. At openings, inset the end posts so their
    # thickness does not reduce the requested clear width.
    corners = np.r_[0, np.cumsum(np.linalg.norm(np.diff(line.coords, axis=0), axis=1))]
    # Axis-aligned square posts extend up to half their diagonal along a fence.
    inset = min(.06, line.length / 4)
    distances = np.unique(np.clip(np.r_[corners, np.linspace(0, line.length,
                                   math.ceil(line.length / spacing) + 1)], inset, line.length - inset))
    if line.is_ring:
        distances = distances[:-1]
    meshes = []
    for distance in distances:
        x, y = line.interpolate(distance).coords[0]
        ground = float(elevation(x, y))
        if not math.isfinite(ground):
            raise ValueError("Terrain returned non-finite fence post elevation")
        mesh = trimesh.creation.box(extents=(.08, .08, height + .1))
        mesh.apply_translation((x, y, ground + height / 2))
        meshes.append(mesh)
    result = trimesh.util.concatenate(meshes)
    result.visual = TextureVisuals(material=material)
    return result


def add_fences(scene: trimesh.Scene, features: list, elevation, config: dict) -> dict:
    """Build fences from local metric ways, cutting entrances before clipping.

    Heights default to 1.5 m; posts to 2.5 m spacing; unknown gate widths to 3 m.
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
            for posts in (False, True):
                key = style + ("_posts" if posts else "")
                if key not in material_cache:
                    material_cache[key] = _material(style, posts)
                if posts:
                    mesh = _posts(line, elevation, spacing, height, material_cache[key])
                else:
                    mesh = _panels(line, elevation, spacing, height, style, material_cache[key])
                groups[key].append(mesh)
                sources[key].add(str(feature.get("id", "unknown")))
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
