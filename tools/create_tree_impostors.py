#!/usr/bin/env python3
"""Package existing tree PNGs as four-card, self-contained glTF 2.0 impostors.

Pillow measures alpha coverage only; original PNG bytes are embedded unchanged.
Each model has four quads/eight triangles, Y up, meters, and a trunk-base origin.
"""

import argparse
import io
import json
import math
from pathlib import Path
from statistics import median
import struct

from PIL import Image


ASSET_DIR = Path(__file__).resolve().parents[1] / "assets"
ALPHA_THRESHOLD = 32
ALPHA_CUTOFF = 0.35
PLANES = ("front", "side", "top-mid", "top-two-thirds")


def _read_texture(path):
    data = path.read_bytes()
    with Image.open(io.BytesIO(data)) as image:
        if image.format != "PNG":
            raise ValueError(f"Expected a PNG texture: {path}")
        if "A" not in image.getbands() and "transparency" not in image.info:
            raise ValueError(f"Texture requires an alpha channel: {path}")
        alpha = image.convert("RGBA").getchannel("A")
        mask = alpha.point(lambda value: 255 if value >= ALPHA_THRESHOLD else 0)
        bounds = mask.getbbox()
        if bounds is None:
            raise ValueError(f"Texture has no visible pixels: {path}")
        width, canvas_height = image.size
        left, top, right, bottom = bounds
        # Pixel edges define height; pixel centers locate the trunk horizontally.
        # Only the lowest 5% of the silhouette contributes, avoiding the crown.
        band_top = max(top, bottom - max(1, math.ceil((bottom - top) * 0.05)))
        pixels = mask.load()
        trunk_x = median(x + 0.5 for y in range(band_top, bottom)
                         for x in range(left, right) if pixels[x, y])
    return data, {
        "file": path.name,
        "canvasPixels": [width, canvas_height],
        "visibleBoundsPixels": list(bounds),
        "trunkBasePixels": [trunk_x, bottom],
    }


def build_impostor(species, height, asset_dir=ASSET_DIR):
    """Return deterministic GLB bytes for ``tree`` or ``pine`` and visible height.

    ``asset_dir`` contains species.png, species-side.png, species-top-mid.png,
    and species-top-two-thirds.png. Full texture canvases and UVs are retained.
    Horizontal cards share a centered canvas calibrated from the front image;
    their alpha coverage never changes their individual scale or placement.
    """
    if species not in ("tree", "pine"):
        raise ValueError("Species must be 'tree' or 'pine'")
    if not math.isfinite(height) or height <= 0:
        raise ValueError("Visible height must be finite and greater than zero")
    asset_dir = Path(asset_dir)
    textures = [_read_texture(asset_dir / f"{species}{suffix}.png")
                for suffix in ("", "-side", "-top-mid", "-top-two-thirds")]
    sources = [metadata for _, metadata in textures]
    if sources[2]["canvasPixels"] != sources[3]["canvasPixels"]:
        raise ValueError("Horizontal slices must share the same canvas dimensions")

    for source in sources[:2]:
        _, top, _, bottom = source["visibleBoundsPixels"]
        source["metersPerPixel"] = height / (bottom - top)
        source["pivotPixels"] = source.pop("trunkBasePixels")
    front_scale = sources[0]["metersPerPixel"]
    for source in sources[2:]:
        source.pop("trunkBasePixels")
        source["pivotPixels"] = [dimension / 2 for dimension in source["canvasPixels"]]
        source["metersPerPixel"] = front_scale

    binary = bytearray()
    views, accessors = [], []

    def add_view(data, target=None):
        binary.extend(b"\x00" * (-len(binary) % 4))
        view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        binary.extend(data)
        views.append(view)
        return len(views) - 1

    def add_vectors(values, dimension, bounds=False):
        flat = [component for value in values for component in value]
        view = add_view(struct.pack("<" + "f" * len(flat), *flat), 34962)
        accessor = {"bufferView": view, "componentType": 5126,
                    "count": len(values), "type": f"VEC{dimension}"}
        if bounds:
            # Match the actual float32 buffer, including extrema after rounding.
            stored = struct.unpack("<" + "f" * len(flat), struct.pack("<" + "f" * len(flat), *flat))
            accessor["min"] = [min(stored[axis::dimension]) for axis in range(dimension)]
            accessor["max"] = [max(stored[axis::dimension]) for axis in range(dimension)]
        accessors.append(accessor)
        return len(accessors) - 1

    indices_view = add_view(struct.pack("<6H", 0, 1, 2, 0, 2, 3), 34963)
    accessors.append({"bufferView": indices_view, "componentType": 5123,
                      "count": 6, "type": "SCALAR", "min": [0], "max": [3]})
    # Image top-left, bottom-left, bottom-right, top-right. All cards wind outward.
    uvs = [(0, 0), (0, 1), (1, 1), (1, 0)]
    uv_accessor = add_vectors(uvs, 2)
    primitives, planes = [], []
    for index, (plane, source) in enumerate(zip(PLANES, sources)):
        width, canvas_height = source["canvasPixels"]
        pivot_x, pivot_y = source["pivotPixels"]
        scale = source["metersPerPixel"]
        positions = []
        fraction = None if index < 2 else (0.5 if index == 2 else 2 / 3)
        for u, v in uvs:
            horizontal = (u * width - pivot_x) * scale
            if index < 2:
                vertical = (pivot_y - v * canvas_height) * scale
                positions.append((horizontal, vertical, 0) if index == 0
                                 else (0, vertical, -horizontal))
            else:
                positions.append((horizontal, height * fraction,
                                  (v * canvas_height - pivot_y) * scale))
        normal = ((0, 0, 1), (1, 0, 0), (0, 1, 0), (0, 1, 0))[index]
        metadata = {"name": plane, "plane": ("XY", "YZ", "XZ", "XZ")[index],
                    "normal": list(normal), "collision": False}
        if fraction is not None:
            metadata.update({"heightFraction": fraction, "heightMeters": height * fraction})
        planes.append(metadata)
        primitives.append({
            "attributes": {"POSITION": add_vectors(positions, 3, True),
                           "NORMAL": add_vectors([normal] * 4, 3),
                           "TEXCOORD_0": uv_accessor},
            "indices": 0, "material": index, "mode": 4, "extras": metadata,
        })

    images = [{"name": source["file"], "bufferView": add_view(data), "mimeType": "image/png"}
              for data, source in textures]
    metadata = {
        "renderOnly": True, "collision": False, "units": "meters", "upAxis": "+Y",
        "origin": "visible trunk base", "visibleHeightMeters": height,
        "alphaBoundsThreshold": ALPHA_THRESHOLD, "alphaCutoff": ALPHA_CUTOFF,
        "trunkPivotMethod": "median pixel center in bottom 5% of alpha silhouette",
        "sourceTextures": sources, "planes": planes,
        "horizontalCanvasMeters": [dimension * front_scale
                                   for dimension in sources[2]["canvasPixels"]],
    }
    document = {
        "asset": {"version": "2.0", "generator": "GDL tree impostor packager"},
        "extensionsUsed": ["KHR_materials_unlit"],
        "scene": 0, "scenes": [{"name": f"{species} impostor", "nodes": [0]}],
        "nodes": [{"name": f"{species}_impostor", "mesh": 0,
                   "extras": {"renderOnly": True, "collision": False}}],
        "meshes": [{"name": f"{species}_impostor", "primitives": primitives, "extras": metadata}],
        "materials": [{
            "name": f"{species}_{plane}",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": index},
                                    "baseColorFactor": [1, 1, 1, 1],
                                    "metallicFactor": 0, "roughnessFactor": 1},
            "alphaMode": "MASK", "alphaCutoff": ALPHA_CUTOFF, "doubleSided": True,
            "extensions": {"KHR_materials_unlit": {}},
        } for index, plane in enumerate(PLANES)],
        "textures": [{"sampler": 0, "source": index} for index in range(4)],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 33071, "wrapT": 33071}],
        "images": images, "accessors": accessors, "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}], "extras": metadata,
    }
    json_bytes = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    json_bytes += b" " * (-len(json_bytes) % 4)
    binary.extend(b"\x00" * (-len(binary) % 4))
    length = 12 + 8 + len(json_bytes) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, length)
            + struct.pack("<I4s", len(json_bytes), b"JSON") + json_bytes
            + struct.pack("<I4s", len(binary), b"BIN\x00") + binary)


def _positive_height(value):
    height = float(value)
    if not math.isfinite(height) or height <= 0:
        raise argparse.ArgumentTypeError("height must be finite and greater than zero")
    return height


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ASSET_DIR)
    parser.add_argument("--tree-height", type=_positive_height, default=10.0, metavar="METERS")
    parser.add_argument("--pine-height", type=_positive_height, default=14.0, metavar="METERS")
    args = parser.parse_args()
    # Validate both species before writing either output.
    models = [(species, height, build_impostor(species, height))
              for species, height in (("tree", args.tree_height), ("pine", args.pine_height))]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for species, height, data in models:
        output = args.output_dir / f"{species}_impostor.glb"
        output.write_bytes(data)
        print(f"Generated {output} ({len(data):,} bytes): {height:g} m visible height, "
              f"4 planes / 8 triangles / 4 embedded PNGs; slices at {height / 2:g} m "
              f"and {height * 2 / 3:g} m")


if __name__ == "__main__":
    main()
