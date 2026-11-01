#!/usr/bin/env python3
"""Apply the supplied surface palette to Lowbit Lab's cached undead dog GLB."""
from __future__ import annotations

import argparse
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import zipfile

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.glb import pack_glb, read_glb
from src.undead_dog_geometry import flatten_tongue
from src.undead_dog_variants import APPEARANCES, BONE_WHITE, RIB_PATCHES_64, appearance_atlas

ASSETS = ROOT / "assets/undead_dog"
MEMBER = "PSX_Horror_NPCs_and_Monsters/Models/GLB/dog.glb"
PAGE = "https://lowbitlab.itch.io/free-psx-horror-npcs-sampler"
PALETTE_PATH = ROOT / "assets/palettes/undead.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def surface_palette(reference):
    """Sample swatch interiors, excluding black padding, text and emissive rows.

    Coordinates are fractions of the supplied square reference. Its neutral
    column is twice as wide as the surrounding swatches. Median sampling avoids
    the blurred boundaries in the reference without inventing intermediate hues.
    """
    reference = np.asarray(reference.convert("RGB"))
    height, width = reference.shape[:2]
    if height != width:
        raise ValueError("Expected the supplied square palette reference")
    xs = [40 + 80 * i for i in range(8)] + [840 + 80 * i for i in range(6)]
    centers = [(x, y) for y in (80, 240, 400) for x in xs]
    centers += [(720, y) for y in (40, 120, 200, 280, 360, 440)]
    centers += [(40 + 80 * i, 560) for i in range(5)]
    colors = []
    for x, y in centers:
        cx, cy = round(x * width / 1280), round(y * height / 1280)
        radius = max(1, round(10 * width / 1280))
        patch = reference[cy-radius:cy+radius+1, cx-radius:cx+radius+1]
        colors.append(np.median(patch, axis=(0, 1)).astype(np.uint8))
    return np.unique(colors, axis=0)


def oklab(rgb):
    """Convert sRGB samples to perceptual coordinates for nearest swatch lookup."""
    rgb = np.asarray(rgb, dtype=np.float64) / 255
    linear = np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4)
    lms = linear @ np.array([
        [.4122214708, .5363325363, .0514459929],
        [.2119034982, .6806995451, .1073969566],
        [.0883024619, .2817188376, .6299787005],
    ]).T
    return np.cbrt(lms) @ np.array([
        [.2104542553, .7936177850, -.0040720468],
        [1.9779984951, -2.4285922050, .4505937099],
        [.0259040371, .7827717662, -.8086757660],
    ]).T


def modal_downscale(pixels, size):
    """Select the most frequent complete RGBA color in each source block.

    Ties use the first tied color encountered in row-major source order. Each
    size is reduced directly from the full atlas, avoiding chained reductions.
    """
    height, width, channels = pixels.shape
    if size <= 0 or height != width or height % size:
        raise ValueError("Texture size must divide the square source atlas")
    factor = height // size
    result = np.empty((size, size, channels), dtype=pixels.dtype)
    for y in range(size):
        for x in range(size):
            block = pixels[y*factor:(y+1)*factor, x*factor:(x+1)*factor].reshape(-1, channels)
            colors, first, counts = np.unique(block, axis=0, return_index=True, return_counts=True)
            tied = np.flatnonzero(counts == counts.max())
            result[y, x] = colors[tied[np.argmin(first[tied])]]
    return result


def prepare(source, palette, texture_size=128):
    document, binary = read_glb(source)
    if len(document.get("images", [])) != 1:
        raise ValueError("Expected the dog's single embedded atlas")
    image = document["images"][0]
    view = document["bufferViews"][image["bufferView"]]
    start = view.get("byteOffset", 0)
    pixels = np.asarray(Image.open(io.BytesIO(binary[start:start + view["byteLength"]])).convert("RGBA")).copy()
    rgb, inverse = np.unique(pixels[:, :, :3].reshape(-1, 3), axis=0, return_inverse=True)
    distances = ((oklab(rgb)[:, None, :] - oklab(palette)[None, :, :]) ** 2).sum(axis=2)
    pixels[:, :, :3] = palette[distances.argmin(axis=1)][inverse].reshape(pixels.shape[:2] + (3,))
    if texture_size != pixels.shape[0]:
        pixels = modal_downscale(pixels, texture_size)
    stream = io.BytesIO()
    Image.fromarray(pixels).save(stream, format="PNG")
    texture = stream.getvalue()
    return embed_atlas(source, texture), texture


def embed_atlas(source, texture):
    document, binary = read_glb(source)
    result = copy.deepcopy(document)
    image = result["images"][0]
    # Append instead of repacking: every geometry, skin and animation byte and
    # every existing accessor retains its original offset and contents.
    offset = len(binary) + (-len(binary) % 4)
    binary += b"\0" * (-len(binary) % 4) + texture
    image["bufferView"] = len(result["bufferViews"])
    image["name"] = "dog_atlas_palette"
    result["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(texture)})
    result["buffers"][0]["byteLength"] = len(binary)
    output = pack_glb(result, binary)
    read_glb(output)
    return output


def rib_detail_atlas(atlas16, atlas64):
    coarse = np.asarray(Image.open(io.BytesIO(atlas16)).convert("RGBA"))
    detailed = np.asarray(Image.open(io.BytesIO(atlas64)).convert("RGBA"))
    if coarse.shape != (16, 16, 4) or detailed.shape != (64, 64, 4):
        raise ValueError("Rib detail requires the 16px and 64px dog atlases")
    pixels = coarse.repeat(4, axis=0).repeat(4, axis=1)
    for left, top, right, bottom in RIB_PATCHES_64:
        pixels[top:bottom, left:right] = detailed[top:bottom, left:right]
    # The exposed ribs contain two baked bone-shadow colors. Match only these
    # bone texels to the opposite flank's white; preserve flesh, blood and alpha.
    exposed_rgb = pixels[48:64, 0:16, :3]
    bone_shadow = np.zeros(exposed_rgb.shape[:2], dtype=bool)
    for color in ((137, 133, 124), (193, 178, 173)):
        bone_shadow |= np.all(exposed_rgb == color, axis=2)
    exposed_rgb[bone_shadow] = detailed[21, 36, :3]
    stream = io.BytesIO()
    Image.fromarray(pixels).save(stream, format="PNG")
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ASSETS / "source/PSX_Horror_NPCs_and_Monsters_FREE_Sample.zip")
    args = parser.parse_args()
    archive = args.archive.read_bytes()
    reference = (ASSETS / "palette-reference.png").read_bytes()
    manifest_path = ASSETS / "sources.json"
    if manifest_path.exists():
        pinned = json.loads(manifest_path.read_text())
        if digest(archive) != pinned["archive_sha256"] or digest(reference) != pinned["palette_sha256"]:
            raise ValueError("Source or palette changed; review sources.json before rebuilding")
    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        source = bundle.read(MEMBER)
    saved_palette = json.loads(PALETTE_PATH.read_text())
    palette = np.asarray(saved_palette["surface_rgb"], dtype=np.uint8)
    sampled = surface_palette(Image.open(io.BytesIO(reference)))
    if saved_palette.get("schema_version") != 1 or not np.array_equal(palette, sampled):
        raise ValueError("Saved surface palette must match the reference swatches")
    if saved_palette["reference_sha256"] != digest(reference):
        raise ValueError("Saved palette reference fingerprint mismatch")
    output, texture = prepare(source, palette)
    output_path = ASSETS / "models/undead_dog.glb"
    atomic_write(output_path, output)
    atomic_write(output_path.with_name("dog_atlas.png"), texture)
    manifest = {
        "source_page": PAGE, "author": "Lowbit Lab", "archive_name": args.archive.name,
        "archive_sha256": digest(archive), "model_member": MEMBER, "model_sha256": digest(source),
        "palette_sha256": digest(reference), "surface_swatches": palette.tolist(),
        "palette_file": str(PALETTE_PATH.relative_to(ROOT)),
        "palette_file_sha256": digest(PALETTE_PATH.read_bytes()),
        "output_sha256": digest(output), "atlas_sha256": digest(texture),
        "preparation": "tools/prepare_undead_dog.py",
    }
    manifest["texture_variants"] = {}
    atlases = {}
    for size in (64, 32, 16):
        variant, atlas = prepare(source, palette, texture_size=size)
        atlases[size] = atlas
        model_name = f"undead_dog_{size}.glb"
        atlas_name = f"dog_atlas_{size}.png"
        atomic_write(output_path.with_name(model_name), variant)
        atomic_write(output_path.with_name(atlas_name), atlas)
        manifest["texture_variants"][str(size)] = {
            "model": model_name, "atlas": atlas_name,
            "output_sha256": digest(variant), "atlas_sha256": digest(atlas),
            "reduction": "most common RGBA color per original atlas block; ties select first occurrence",
        }
    appearance_source, geometry = flatten_tongue(source)
    manifest["appearance_geometry"] = geometry
    atlas = rib_detail_atlas(atlases[16], atlases[64])
    variant = embed_atlas(appearance_source, atlas)
    model_name = "undead_dog_16_ribs64.glb"
    atlas_name = "dog_atlas_16_ribs64.png"
    atomic_write(output_path.with_name(model_name), variant)
    atomic_write(output_path.with_name(atlas_name), atlas)
    manifest["texture_variants"]["16_ribs64"] = {
        "model": model_name, "atlas": atlas_name,
        "output_sha256": digest(variant), "atlas_sha256": digest(atlas),
        "reduction": "16px block-mode base enlarged to 64px by nearest neighbor; rib patches copied from 64px atlas",
        "rib_rectangles_64px": RIB_PATCHES_64,
        "rib_color": "exposed rib bone shadows use the painted flank's bone white, RGB 209,204,201",
    }
    manifest["appearance_variants"] = {}
    for number, description in APPEARANCES.items():
        appearance = appearance_atlas(atlas, number, palette)
        model = embed_atlas(appearance_source, appearance)
        model_name = f"undead_dog_variant_{number}.glb"
        atlas_name = f"dog_atlas_variant_{number}.png"
        atomic_write(output_path.with_name(model_name), model)
        atomic_write(output_path.with_name(atlas_name), appearance)
        manifest["appearance_variants"][str(number)] = {
            "name": description, "model": model_name, "atlas": atlas_name,
            "output_sha256": digest(model), "atlas_sha256": digest(appearance),
            "rib_rgb": BONE_WHITE,
        }
    atomic_write(manifest_path, (json.dumps(manifest, indent=2) + "\n").encode())
    print(f"Prepared texture resolutions and appearance variants 1–3 under {output_path.parent.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
