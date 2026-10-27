"""Validate, export and preview the original ground-detail mesh assets."""
from __future__ import annotations

from collections import defaultdict
import math
from pathlib import Path
import struct

from .glb import pack_glb


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def prepared_parts(model):
    """Validate the closed shell after float32 conversion, across material seams."""
    edges = defaultdict(list)
    volume = 0.0
    result = []
    for part in model["parts"]:
        source = [struct.unpack("<3f", struct.pack("<3f", *p)) for p in part["positions"]]
        if not source or not all(math.isfinite(v) for p in source for v in p):
            raise ValueError(f"Invalid positions in {part['name']}")
        color = part["color"]
        if len(color) != 3 or not all(math.isfinite(c) and 0 <= c <= 1 for c in color):
            raise ValueError(f"Invalid color in {part['name']}")
        roughness = part.get("roughness", 1.0)
        if not math.isfinite(roughness) or not 0 <= roughness <= 1:
            raise ValueError(f"Invalid roughness in {part['name']}")
        positions, normals = [], []
        for triangle in part["triangles"]:
            if len(triangle) != 3 or any(type(i) is not int or not 0 <= i < len(source) for i in triangle):
                raise ValueError(f"Invalid triangle in {part['name']}")
            a, b, c = (source[i] for i in triangle)
            normal = cross(tuple(b[i]-a[i] for i in range(3)), tuple(c[i]-a[i] for i in range(3)))
            length = math.sqrt(sum(v*v for v in normal))
            if length < 1e-12:
                raise ValueError(f"Degenerate triangle in {part['name']}")
            positions.extend((a, b, c))
            normals.extend([tuple(v/length for v in normal)]*3)
            volume += sum(a[i]*cross(b, c)[i] for i in range(3))/6
            for u, v in ((a, b), (b, c), (c, a)):
                edges[tuple(sorted((u, v)))].append((u, v))
        if not positions:
            raise ValueError(f"Empty part: {part['name']}")
        result.append((part, positions, normals))
    if not edges or any(len(uses) != 2 or uses[0] != uses[1][::-1] for uses in edges.values()):
        raise ValueError(f"{model['id']}: shell is open, nonmanifold, or inconsistently wound")
    if volume <= 0:
        raise ValueError(f"{model['id']}: expected outward-facing solid")
    return result


def build_glb(model):
    binary = bytearray()
    views, accessors, materials, primitives = [], [], [], []

    def vectors(values, bounds=False):
        binary.extend(b"\0" * (-len(binary) % 4))
        data = struct.pack("<" + "f"*(len(values)*3), *(v for p in values for v in p))
        views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(data), target=34962))
        binary.extend(data)
        accessor = dict(bufferView=len(views)-1, componentType=5126, count=len(values), type="VEC3")
        if bounds:
            accessor.update(min=[min(p[i] for p in values) for i in range(3)],
                            max=[max(p[i] for p in values) for i in range(3)])
        accessors.append(accessor)
        return len(accessors)-1

    for part, positions, normals in prepared_parts(model):
        attributes = dict(POSITION=vectors(positions, True), NORMAL=vectors(normals))
        primitives.append(dict(attributes=attributes, material=len(materials)))
        materials.append(dict(name=part["name"], pbrMetallicRoughness=dict(
            baseColorFactor=part["color"]+[1], metallicFactor=0,
            roughnessFactor=part.get("roughness", 1.0))))
    document = dict(
        asset=dict(version="2.0", generator="GDL original ground asset builder",
                   copyright=model["copyright"]),
        scene=0, scenes=[dict(nodes=[0])], nodes=[dict(name=model["id"], mesh=0)],
        meshes=[dict(name=model["id"], primitives=primitives)], materials=materials,
        accessors=accessors, bufferViews=views, buffers=[dict(byteLength=len(binary))],
        extras=dict(units="meters", axes="X east, Y up, Z south", **model.get("metadata", {})))
    return pack_glb(document, bytes(binary))


def render_previews(outputs, directory):
    """Render the exported geometry, with a second view exposing its topology."""
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
    from src.vehicle_asset_render import geometry, render

    font_path = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    def font(size):
        return ImageFont.truetype(str(font_path), size) if font_path.exists() else ImageFont.load_default()

    labels = {"branch": ("Y-shaped branch", "Concave nonagon base / triangular arms"),
              "stone": ("Forest stone", "Irregular pentagonal frustum"),
              "beer_bottle": ("Beer bottle", "Hexagonal body / shoulder / neck")}
    directory.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        meshes = geometry(data)
        vertices = np.concatenate([m["vertices"] for m in meshes])
        low, high = vertices.min(0), vertices.max(0)
        center = (low+high)/2
        # Fit projected bounds: the shared renderer normally favors vehicles'
        # wide silhouettes, which can otherwise crop these taller assets.
        def view(direction, size, wire=False):
            direction = np.asarray(direction, dtype=float)
            direction /= np.linalg.norm(direction)
            right = np.cross([0, 1, 0], direction)
            right /= np.linalg.norm(right)
            up = np.cross(direction, right)
            span = np.ptp((vertices-center) @ np.array([right, up]).T, axis=0)
            scale = min(size[0]*.85/span[0], size[1]*.85/span[1])
            framing_radius = min(size[0]/2.2, size[1]/1.45)/scale
            return render(meshes, (center, np.eye(3), framing_radius), direction,
                          size=size, supersample=2, wire=wire)

        direction = (.45, 1.9, 1.1) if name == "branch" else (1.1, .85, 1.5)
        hero = view(direction, size=(760, 610))
        detail_direction = (.01, 1, .01) if name != "beer_bottle" else (1.1, .65, 1.5)
        detail = view(detail_direction, size=(320, 330), wire=True)
        page = Image.new("RGB", (1120, 800), (23, 29, 37))
        page.paste(hero, (10, 115))
        page.paste(detail, (790, 200))
        draw = ImageDraw.Draw(page)
        title, description = labels[name]
        draw.text((36, 23), title, fill=(239, 234, 220), font=font(34))
        draw.text((38, 72), description, fill=(167, 181, 193), font=font(19))
        draw.text((808, 560), "Topology view", fill=(167, 181, 193), font=font(18))
        count = sum(len(m["faces"]) for m in meshes)
        dimensions = " x ".join(f"{v*100:.1f}" for v in high-low)
        draw.text((38, 750), f"{count} triangles    |    X / Y / Z: {dimensions} cm", fill=(189, 201, 210), font=font(19))
        page.save(directory / f"{name}.png")
