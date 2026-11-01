#!/usr/bin/env python3
"""Recolor the cached dudecon pigeon atlas using the shared undead palette."""
from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import zipfile

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets/pigeon"
ARCHIVE_URL = "https://github.com/user-attachments/files/18528328/animated-bird-pigeon.zip"
MODEL_URL = "https://sketchfab.com/3d-models/animated-bird-pigeon-797d27b68af3453e865149435df6aa30"
WARM_WHITE = (209, 204, 201)
YELLOW_BEIGE = (165, 161, 152)
LIGHT_FEATHERS = ((222, 217, 214), (253, 253, 253))


def oklab(rgb):
    """Perceptual coordinates from 8-bit sRGB, for nearest-swatch matching."""
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


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    archive = (ASSETS / "source/animated-bird-pigeon.zip").read_bytes()
    manifest_path = ASSETS / "sources.json"
    if manifest_path.exists():
        pinned = json.loads(manifest_path.read_text())
        if pinned["archive_sha256"] != digest(archive):
            raise ValueError("Source archive changed; review sources.json before rebuilding")
    with zipfile.ZipFile(io.BytesIO(archive)) as outer:
        with zipfile.ZipFile(io.BytesIO(outer.read("source/animated bird.zip"))) as inner:
            model = inner.read("bird animation tweaks.fbx")
            original_texture = inner.read("Bird Texture.jpg")
    (ASSETS / "source/pigeon.fbx").write_bytes(model)
    (ASSETS / "source/Bird Texture.jpg").write_bytes(original_texture)

    palette_path = ROOT / "assets/palettes/undead.json"
    saved = json.loads(palette_path.read_text())
    reference = ROOT / "assets/undead_dog/palette-reference.png"
    if saved["schema_version"] != 1 or saved["reference_sha256"] != digest(reference.read_bytes()):
        raise ValueError("Palette reference fingerprint mismatch")
    palette = np.array(saved["surface_rgb"], dtype=np.uint8)
    pixels = np.asarray(Image.open(io.BytesIO(original_texture)).convert("RGB"))
    colors, inverse = np.unique(pixels.reshape(-1, 3), axis=0, return_inverse=True)
    distances = ((oklab(colors)[:, None] - oklab(palette)[None]) ** 2).sum(axis=2)
    recolored = palette[distances.argmin(axis=1)][inverse].reshape(pixels.shape)
    output = ASSETS / "models/pigeon_atlas.png"
    image = Image.fromarray(recolored)
    image.save(output)
    small_output = ASSETS / "models/pigeon_atlas_16.png"
    small = image.resize((16, 16), Image.Resampling.NEAREST)
    small.save(small_output)
    assert WARM_WHITE in set(map(tuple, palette))
    warm_pixels = np.asarray(small).copy()
    light_mask = np.zeros(warm_pixels.shape[:2], dtype=bool)
    for color in LIGHT_FEATHERS:
        light_mask |= np.all(warm_pixels == color, axis=2)
    warm_pixels[light_mask] = WARM_WHITE
    warm_output = ASSETS / "models/pigeon_atlas_16_warm.png"
    Image.fromarray(warm_pixels).save(warm_output)
    assert YELLOW_BEIGE in set(map(tuple, palette))
    yellow_pixels = warm_pixels.copy()
    yellow_mask = np.all(yellow_pixels == WARM_WHITE, axis=2)
    yellow_pixels[yellow_mask] = YELLOW_BEIGE
    yellow_output = ASSETS / "models/pigeon_atlas_16_yellow.png"
    Image.fromarray(yellow_pixels).save(yellow_output)
    actual = set(map(tuple, np.unique(recolored.reshape(-1, 3), axis=0)))
    assert actual <= set(map(tuple, palette))
    manifest = {
        "author": "Paul Spooner (dudecon / Peripheral Arbor)",
        "model_page": MODEL_URL,
        "download_url": ARCHIVE_URL,
        "archive_context": "https://github.com/orgs/assimp/discussions/5970",
        "archive_sha256": digest(archive),
        "model_sha256": digest(model),
        "source_atlas_sha256": digest(original_texture),
        "license": "CC BY 4.0 as listed on Sketchfab; author additionally declares public domain",
        "author_public_domain_declaration": "https://ip.tryop.com/",
        "palette": "assets/palettes/undead.json",
        "palette_sha256": digest(palette_path.read_bytes()),
        "reference_sha256": saved["reference_sha256"],
        "atlas_sha256": digest(output.read_bytes()),
        "texture_variants": {
            "16": {
                "atlas": "models/pigeon_atlas_16.png",
                "atlas_sha256": digest(small_output.read_bytes()),
                "downscale": "512x512 to 16x16 using nearest-neighbor sampling",
            },
            "16_warm": {
                "atlas": "models/pigeon_atlas_16_warm.png",
                "atlas_sha256": digest(warm_output.read_bytes()),
                "source_variant": "16",
                "replace_rgb": LIGHT_FEATHERS,
                "with_rgb": WARM_WHITE,
                "changed_texels": int(light_mask.sum()),
            },
            "16_yellow": {
                "atlas": "models/pigeon_atlas_16_yellow.png",
                "atlas_sha256": digest(yellow_output.read_bytes()),
                "source_variant": "16_warm",
                "replace_rgb": [WARM_WHITE],
                "with_rgb": YELLOW_BEIGE,
                "changed_texels": int(yellow_mask.sum()),
            },
        },
        "surface_colors_used": len(actual),
        "recoloring": "Nearest surface swatch in Oklab; original 512px texture and UVs retained; no emissive material",
        "preparation": "tools/prepare_pigeon.py",
        "rendering": "tools/render_pigeon.py",
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared {output.relative_to(ROOT)} using {len(actual)} exact surface swatches")


if __name__ == "__main__":
    main()
