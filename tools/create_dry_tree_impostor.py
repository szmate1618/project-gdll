#!/usr/bin/env python3
"""Build the asymmetric dry tree's trunk card and offset upper-branch card."""

import argparse
import io
import json
import math
from pathlib import Path
import struct
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.glb import pack_glb, read_glb


ASSET_DIR = ROOT / "assets/dry_tree"
ALPHA_CUTOFF = 0.35


def build_impostor(asset_dir=ASSET_DIR, height=5.0):
    """Return a Y-up, meter-scale GLB with its origin at the painted root.

    Authored pixel anchors join the branch card to the upper trunk, rather
    than centering a second full-height card at the root. PNG bytes stay intact.
    This two-card asset has separate metadata from the four-card tree format.
    """
    if not math.isfinite(height) or height <= 0:
        raise ValueError("Height must be finite and positive")
    asset_dir = Path(asset_dir)
    layout = json.loads((asset_dir / "layout.json").read_text())
    sources = []
    for name in ("front", "branches"):
        source = dict(layout[name])
        data = (asset_dir / source["file"]).read_bytes()
        with Image.open(io.BytesIO(data)) as image:
            if image.format != "PNG" or "A" not in image.getbands():
                raise ValueError("Dry tree textures must be RGBA PNGs")
            bounds = image.getchannel("A").point(
                lambda alpha: 255 if alpha >= 255 * ALPHA_CUTOFF else 0).getbbox()
            if bounds is None:
                raise ValueError("Dry tree texture has no visible pixels")
            source.update(canvasPixels=list(image.size), visibleBoundsPixels=list(bounds))
        source["data"] = data
        sources.append(source)

    front, branches = sources
    root_x, root_y = front["rootPixels"]
    top = front["visibleBoundsPixels"][1]
    if root_y <= top:
        raise ValueError("Root must be below the visible crown")
    scale = height / (root_y - top)
    join_x, join_y = front["branchJoinPixels"]
    join = [(join_x - root_x) * scale, (root_y - join_y) * scale, 0.0]
    anchor_x, anchor_y = branches["joinPixels"]
    branch_top = branches["visibleBoundsPixels"][1]
    if not 0 < join[1] < height or anchor_y <= branch_top:
        raise ValueError("Branch join must lie between the root and crown")
    branch_scale = (height - join[1]) / (anchor_y - branch_top)

    binary, views, accessors = bytearray(), [], []

    def view(data, target=None):
        binary.extend(b"\0" * (-len(binary) % 4))
        item = dict(buffer=0, byteOffset=len(binary), byteLength=len(data))
        if target is not None:
            item["target"] = target
        views.append(item)
        binary.extend(data)
        return len(views) - 1

    def vectors(values, dimensions, bounds=False):
        flat = [v for row in values for v in row]
        encoded = struct.pack(f"<{len(flat)}f", *flat)
        item = dict(bufferView=view(encoded, 34962), componentType=5126,
                    count=len(values), type=f"VEC{dimensions}")
        if bounds:
            stored = struct.unpack(f"<{len(flat)}f", encoded)
            item.update(min=[min(stored[i::dimensions]) for i in range(dimensions)],
                        max=[max(stored[i::dimensions]) for i in range(dimensions)])
        accessors.append(item)
        return len(accessors) - 1

    accessors.append(dict(bufferView=view(struct.pack("<6H", 0, 1, 2, 0, 2, 3), 34963),
                          componentType=5123, count=6, type="SCALAR", min=[0], max=[3]))
    uv = [(0, 0), (0, 1), (1, 1), (1, 0)]
    uv_index = vectors(uv, 2)
    primitives, images = [], []
    for index, source in enumerate(sources):
        width, canvas_height = source["canvasPixels"]
        positions = []
        for u, v in uv:
            if index == 0:
                positions.append(((u * width - root_x) * scale,
                                  (root_y - v * canvas_height) * scale, 0.0))
            else:
                positions.append((join[0], join[1] + (anchor_y - v * canvas_height) * branch_scale,
                                  (anchor_x - u * width) * branch_scale))
        normal = (0, 0, 1) if index == 0 else (1, 0, 0)
        primitives.append(dict(attributes=dict(POSITION=vectors(positions, 3, True),
                                                NORMAL=vectors([normal] * 4, 3),
                                                TEXCOORD_0=uv_index),
                               indices=0, material=index, mode=4))
        images.append(dict(name=source["file"], bufferView=view(source.pop("data")), mimeType="image/png"))

    metadata = dict(schema="godollo.dry-tree-impostor", version=1, units="meters", upAxis="+Y",
                    origin="visible trunk base", renderOnly=True, collision=False,
                    visibleHeightMeters=height, branchJoinMeters=join,
                    sourceTextures=sources, metersPerPixel=[scale, branch_scale])
    document = dict(
        asset=dict(version="2.0", generator="GDL dry tree impostor packager"),
        extensionsUsed=["KHR_materials_unlit"], scene=0,
        scenes=[dict(nodes=[0])], nodes=[dict(name="dry_tree_impostor", mesh=0)],
        meshes=[dict(name="dry_tree_impostor", primitives=primitives, extras=metadata)],
        materials=[dict(name=name, alphaMode="MASK", alphaCutoff=ALPHA_CUTOFF, doubleSided=True,
                        extensions={"KHR_materials_unlit": {}},
                        pbrMetallicRoughness=dict(baseColorTexture=dict(index=i), metallicFactor=0,
                                                 roughnessFactor=1))
                   for i, name in enumerate(("dry_trunk", "dry_upper_branches"))],
        textures=[dict(source=i, sampler=0) for i in range(2)],
        samplers=[dict(magFilter=9729, minFilter=9987, wrapS=33071, wrapT=33071)],
        images=images, bufferViews=views, accessors=accessors,
        buffers=[dict(byteLength=len(binary))], extras=metadata)
    return pack_glb(document, bytes(binary))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--height", type=float, default=5.0, metavar="METERS")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/dry_tree_impostor.glb")
    args = parser.parse_args()
    data = build_impostor(height=args.height)
    read_glb(data)  # Validate before publishing the generated asset.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(args.output)
    print(f"Generated {args.output}: {args.height:g} m, 2 cards / 4 triangles / 2 embedded PNGs")


if __name__ == "__main__":
    main()
