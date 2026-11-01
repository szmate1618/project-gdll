"""Palette-only coat variations of the approved coarse undead dog texture."""
from __future__ import annotations

import io

import numpy as np
from PIL import Image


# Only coat colors change. Blood reds, flesh pinks and bone white are preserved.
# Entries are (approved source RGB, lighter RGB, near-black RGB), all taken from
# assets/palettes/undead.json. Explicit selections retain warm neutral hues.
COAT_REMAP = (
    ((45, 30, 11), (70, 40, 32), (20, 8, 8)),
    ((64, 36, 32), (77, 60, 52), (24, 8, 9)),
    ((70, 40, 32), (83, 68, 63), (28, 13, 10)),
    ((77, 60, 52), (93, 71, 52), (42, 22, 21)),
    ((93, 71, 52), (115, 87, 76), (42, 22, 21)),
    ((105, 101, 92), (137, 133, 124), (56, 26, 24)),
    ((115, 87, 76), (127, 100, 89), (42, 22, 21)),
    ((127, 100, 89), (137, 133, 124), (42, 22, 21)),
    ((137, 133, 124), (165, 161, 152), (56, 26, 24)),
    ((160, 141, 134), (165, 161, 152), (56, 26, 24)),
    ((165, 161, 152), (193, 178, 173), (70, 66, 57)),
    ((193, 178, 173), (209, 204, 201), (78, 73, 67)),
)

APPEARANCES = {1: "Original", 2: "Lighter", 3: "Near black"}
BONE_WHITE = (209, 204, 201)
# Exclusive right/bottom edges in the 64px atlas. The exposed rib mesh uses the
# bottom-left tile; the opposite flank uses the small torso bone/blood patch.
# These coordinates were checked against the source mesh UVs.
RIB_PATCHES_64 = ((0, 48, 16, 64), (36, 19, 42, 26))


def appearance_atlas(approved_atlas: bytes, variant: int, palette) -> bytes:
    """Return a 64px atlas using only existing palette colors, with alpha intact."""
    if variant not in APPEARANCES:
        raise ValueError(f"Unknown undead dog appearance: {variant}")
    allowed = {tuple(color) for color in palette}
    for row in COAT_REMAP:
        if any(color not in allowed for color in row):
            raise ValueError("Coat mapping uses a color outside the saved palette")
    pixels = np.asarray(Image.open(io.BytesIO(approved_atlas)).convert("RGBA"))
    if pixels.shape != (64, 64, 4):
        raise ValueError("Appearance variants require the approved 64px hybrid atlas")
    if any(tuple(color) not in allowed for color in np.unique(pixels[:, :, :3].reshape(-1, 3), axis=0)):
        raise ValueError("Approved atlas contains a color outside the saved palette")
    if variant == 1:
        return approved_atlas
    result = pixels.copy()
    rgb = pixels[:, :, :3]
    for row in COAT_REMAP:
        result[np.all(rgb == row[0], axis=2), :3] = row[variant - 1]
    # Preserve both restored rib patches exactly, including surrounding blood.
    for left, top, right, bottom in RIB_PATCHES_64:
        result[top:bottom, left:right] = pixels[top:bottom, left:right]
    stream = io.BytesIO()
    Image.fromarray(result).save(stream, format="PNG")
    return stream.getvalue()
