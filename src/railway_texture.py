"""Metric railway artwork and path-aligned UVs, in local east/north metres."""
from __future__ import annotations

from collections import defaultdict, deque
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from shapely import line_locate_point, points

GAUGE_M = 1.435  # Clear distance between the inner edges of the rail heads.
TIE_SPACING_M = 0.6
TEXTURE_WIDTH_M = 4.0
RAIL_HEAD_WIDTH_M = 0.07
TIE_LENGTH_M = 2.6
TIE_WIDTH_M = 0.24
PIXELS_PER_METER = 400  # Gauge and sleeper dimensions land on exact pixel edges.
TEXTURE_PATH = Path(__file__).resolve().parents[1] / "assets/railways/track.png"


def create_track_texture() -> Image.Image:
    """Create one 4 m cross-track by 0.6 m along-track tile, without downloads.

    The vertical edges contain continuous rail heads; one centered timber tie
    leaves gravel between repeats. Stone placement wraps at every tile edge.
    """
    rng = np.random.default_rng(1435)
    width = round(TEXTURE_WIDTH_M * PIXELS_PER_METER)
    height = round(TIE_SPACING_M * PIXELS_PER_METER)
    noise = rng.normal(0, 5, (height, width, 1))
    base = np.clip(np.array([77, 79, 75]) + noise, 0, 255).astype(np.uint8)
    image = Image.fromarray(base)
    draw = ImageDraw.Draw(image)
    for _ in range(6000):
        x, y = rng.uniform(0, width), rng.uniform(0, height)
        radius = rng.uniform(3, 10)
        count = int(rng.integers(5, 8))
        angles = np.linspace(0, 2 * np.pi, count, endpoint=False) + rng.uniform(0, 2 * np.pi)
        radii = radius * rng.uniform(0.65, 1.15, count)
        shape = np.column_stack((np.cos(angles) * radii, np.sin(angles) * radii))
        shade = rng.uniform(94, 151)
        color = tuple(int(v) for v in np.clip(shade + rng.normal(0, 4, 3), 0, 255))
        for dx in (-width, 0, width):
            for dy in (-height, 0, height):
                if -12 < x + dx < width + 12 and -12 < y + dy < height + 12:
                    polygon = [(px + x + dx, py + y + dy) for px, py in shape]
                    draw.polygon(polygon, fill=color, outline=tuple(max(0, c - 28) for c in color))
                    draw.line([polygon[0], polygon[1]], fill=tuple(min(255, c + 15) for c in color))
    # Timber grain follows the tie, across the rails. Rectangular metric bounds
    # stay independent of the random material detail.
    pixels = np.asarray(image).copy()
    x0 = round((TEXTURE_WIDTH_M - TIE_LENGTH_M) * PIXELS_PER_METER / 2)
    x1 = width - x0
    y0 = round((TIE_SPACING_M - TIE_WIDTH_M) * PIXELS_PER_METER / 2)
    y1 = height - y0
    grain = rng.normal(0, 4, (y1 - y0, x1 - x0, 1)) + rng.normal(0, 8, (y1 - y0, 1, 1))
    pixels[y0:y1, x0:x1] = np.clip(np.array([81, 66, 49]) + grain, 0, 255)
    pixels[y0:y0 + 3, x0:x1] //= 2
    pixels[y1 - 3:y1, x0:x1] //= 2
    image = Image.fromarray(pixels)
    draw = ImageDraw.Draw(image)
    for center in (-GAUGE_M / 2 - RAIL_HEAD_WIDTH_M / 2, GAUGE_M / 2 + RAIL_HEAD_WIDTH_M / 2):
        x0 = round((TEXTURE_WIDTH_M / 2 + center - RAIL_HEAD_WIDTH_M / 2) * PIXELS_PER_METER)
        x1 = x0 + round(RAIL_HEAD_WIDTH_M * PIXELS_PER_METER)
        draw.rectangle((x0 - 8, y0 + 16, x1 + 7, y1 - 17), fill=(64, 59, 51))
        draw.rectangle((x0 - 3, 0, x1 + 2, height - 1), fill=(85, 65, 48))
        for x in range(x0, x1):
            shade = int(156 + 72 * np.sin(np.pi * (x - x0 + 0.5) / (x1 - x0)))
            draw.line((x, 0, x, height - 1), fill=(shade, shade + 2, min(255, shade + 5)))
    return image


def track_material():
    """One shared, embedded tile for all railway beds."""
    from trimesh.visual.material import PBRMaterial
    with Image.open(TEXTURE_PATH) as source:
        image = source.convert("RGB")
    return PBRMaterial(name="railway_ballast", baseColorTexture=image,
                       baseColorFactor=[255, 255, 255, 255], metallicFactor=0.0,
                       roughnessFactor=0.85, doubleSided=True)


def track_phases(lines: list) -> list[tuple[float, float]]:
    """Continue tie phase across joined/reversed ways; cycle seams stay local.

    Each pair is (start station in metres, direction sign). Endpoint matching
    uses millimetre-rounded projected coordinates, independent of feature IDs.
    """
    adjacent = defaultdict(list)
    endpoints = []
    for index, line in enumerate(lines):
        ends = [tuple(np.rint(np.asarray(p) * 1000).astype(np.int64))
                for p in (line.coords[0], line.coords[-1])]
        endpoints.append(ends)
        for end in ends:
            adjacent[end].append(index)
    phases = [None] * len(lines)
    stations = {}
    for start, _ in endpoints:
        if start in stations:
            continue
        stations[start] = 0.0
        pending = deque([start])
        while pending:
            node = pending.popleft()
            for index in adjacent[node]:
                if phases[index] is not None:
                    continue
                a, b = endpoints[index]
                length = lines[index].length
                if node == a:
                    phases[index] = (stations[node], 1.0)
                    other, station = b, stations[node] + length
                else:
                    phases[index] = (stations[node] + length, -1.0)
                    other, station = a, stations[node] + length
                if other not in stations:
                    stations[other] = station % TIE_SPACING_M
                    pending.append(other)
    return phases


def track_uv(vertices, line, phase=(0.0, 1.0)) -> np.ndarray:
    """U is transverse metres / 4; V is centerline arclength / 0.6.

    Coordinates may extend outside [0,1]. Width tags change the bed shoulders,
    not the gauge. The exported sampler clamps U and repeats V. UVs follow each
    source line separately, so neighboring tracks cannot steal its direction.
    """
    xy = np.asarray(vertices)[:, :2]
    coords = np.asarray(line.coords)[:, :2]
    vectors = np.diff(coords, axis=0)
    lengths = np.linalg.norm(vectors, axis=1)
    valid = lengths > 1e-9
    if not valid.any():
        raise ValueError("Railway UVs require a nonzero centerline")
    coords = np.vstack((coords[:-1][valid], coords[-1]))
    vectors = np.diff(coords, axis=0)
    lengths = np.linalg.norm(vectors, axis=1)
    stations = np.r_[0.0, np.cumsum(lengths)]
    along = line_locate_point(line, points(xy))
    segment = np.clip(np.searchsorted(stations, along, side="right") - 1, 0, len(lengths) - 1)
    tangent = vectors[segment] / lengths[segment, None]
    center = coords[segment] + tangent * (along - stations[segment])[:, None]
    transverse = tangent[:, 0] * (xy[:, 1] - center[:, 1]) - tangent[:, 1] * (xy[:, 0] - center[:, 0])
    return np.column_stack((0.5 + transverse / TEXTURE_WIDTH_M,
                            (phase[0] + phase[1] * along) / TIE_SPACING_M))


def configure_track_sampler(document: dict) -> None:
    """glTF export callback: repeat along the track, clamp gravel shoulders."""
    for material in document.get("materials", []):
        if material.get("name") != "railway_ballast":
            continue
        texture = material.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if texture is None:
            continue
        samplers = document.setdefault("samplers", [])
        document["textures"][texture["index"]]["sampler"] = len(samplers)
        samplers.append({"wrapS": 33071, "wrapT": 10497, "minFilter": 9987, "magFilter": 9729})
