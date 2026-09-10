#!/usr/bin/env python3
"""Generate a small, self-contained glTF 2.0 binary using only Python's stdlib."""

import argparse
import json
import math
from pathlib import Path
import struct
import zlib


def checker_png():
    """An 8x8 RGB checker image with PNG filtering disabled."""
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    rows = bytearray()
    colors = ((208, 221, 173), (87, 126, 94))
    for y in range(8):
        rows.append(0)
        for x in range(8):
            rows.extend(colors[((x // 2) + (y // 2)) % 2])
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
            + chunk(b"IEND", b""))


def build_glb():
    binary = bytearray()
    buffer_views = []
    accessors = []

    def add_view(data, target=None):
        binary.extend(b"\x00" * (-len(binary) % 4))
        view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        binary.extend(data)
        buffer_views.append(view)
        return len(buffer_views) - 1

    def add_vectors(values, dimension, include_bounds=False):
        flat = [number for value in values for number in value]
        view = add_view(struct.pack("<" + "f" * len(flat), *flat), 34962)
        accessor = {"bufferView": view, "componentType": 5126,
                    "count": len(values), "type": "VEC" + str(dimension)}
        if include_bounds:
            accessor["min"] = [min(value[axis] for value in values) for axis in range(dimension)]
            accessor["max"] = [max(value[axis] for value in values) for axis in range(dimension)]
        accessors.append(accessor)
        return len(accessors) - 1

    def add_indices(values):
        view = add_view(struct.pack("<" + "H" * len(values), *values), 34963)
        accessors.append({"bufferView": view, "componentType": 5123,
                          "count": len(values), "type": "SCALAR",
                          "min": [min(values)], "max": [max(values)]})
        return len(accessors) - 1

    ground_attributes = {
        "POSITION": add_vectors([(-7, 0, 6), (7, 0, 6), (7, 0, -6), (-7, 0, -6)], 3, True),
        "NORMAL": add_vectors([(0, 1, 0)] * 4, 3),
        "TEXCOORD_0": add_vectors([(0, 0), (7, 0), (7, 6), (0, 6)], 2),
    }
    ground_indices = add_indices([0, 1, 2, 0, 2, 3])

    # Separate vertices per face retain crisp normals. Faces are counterclockwise
    # when viewed from outside, including the ground's upward-facing triangles.
    faces = [
        ((1, 0, 0), [(0.5, -0.5, 0.5), (0.5, -0.5, -0.5),
                     (0.5, 0.5, -0.5), (0.5, 0.5, 0.5)]),
        ((-1, 0, 0), [(-0.5, -0.5, -0.5), (-0.5, -0.5, 0.5),
                      (-0.5, 0.5, 0.5), (-0.5, 0.5, -0.5)]),
        ((0, 0, 1), [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5),
                     (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5)]),
        ((0, 0, -1), [(0.5, -0.5, -0.5), (-0.5, -0.5, -0.5),
                      (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5)]),
        ((0, 1, 0), [(-0.5, 0.5, 0.5), (0.5, 0.5, 0.5),
                     (0.5, 0.5, -0.5), (-0.5, 0.5, -0.5)]),
        ((0, -1, 0), [(-0.5, -0.5, -0.5), (0.5, -0.5, -0.5),
                      (0.5, -0.5, 0.5), (-0.5, -0.5, 0.5)]),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, corners in faces:
        base = len(positions)
        positions.extend(corners)
        normals.extend([normal] * 4)
        uvs.extend([(0, 0), (1, 0), (1, 1), (0, 1)])
        indices.extend(base + index for index in (0, 1, 2, 0, 2, 3))
    cube_attributes = {
        "POSITION": add_vectors(positions, 3, True),
        "NORMAL": add_vectors(normals, 3),
        "TEXCOORD_0": add_vectors(uvs, 2),
    }
    sides = add_indices(indices[:24])
    caps = add_indices(indices[24:])
    png_view = add_view(checker_png())

    def y_rotation(degrees):
        half_angle = math.radians(degrees) * 0.5
        return [0, math.sin(half_angle), 0, math.cos(half_angle)]

    angle = math.radians(-30)
    cosine, sine = math.cos(angle), math.sin(angle)
    document = {
        "asset": {"version": "2.0", "generator": "GDL local stdlib test asset generator"},
        "scene": 0,
        "scenes": [{"name": "Offset textured sample", "nodes": [0]}],
        "nodes": [
            {"name": "Offset root", "translation": [120, 0, -75],
             "rotation": y_rotation(15), "children": [1, 2, 5]},
            {"name": "Checker ground", "mesh": 0},
            {"name": "Rotated cube pair", "rotation": y_rotation(25), "children": [3, 4]},
            {"name": "Large cube", "mesh": 1, "translation": [-3, 1.5, -1], "scale": [3, 3, 3]},
            {"name": "Small cube", "mesh": 1, "translation": [3, 1, 2], "scale": [2, 2, 2]},
            {"name": "Matrix-transformed tower", "mesh": 1,
             "matrix": [1.5 * cosine, 0, -1.5 * sine, 0,
                        0, 4, 0, 0,
                        1.5 * sine, 0, 1.5 * cosine, 0,
                        2.5, 2, -3, 1]},
        ],
        "meshes": [
            {"name": "Ground", "primitives": [
                {"attributes": ground_attributes, "indices": ground_indices, "material": 0}]},
            {"name": "Two-material cube", "primitives": [
                {"attributes": cube_attributes, "indices": sides, "material": 1},
                {"attributes": cube_attributes, "indices": caps, "material": 2}]},
        ],
        "materials": [
            {"name": "Checker ground", "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 1}},
            {"name": "Terracotta walls", "pbrMetallicRoughness": {
                "baseColorFactor": [0.85, 0.32, 0.14, 1], "metallicFactor": 0, "roughnessFactor": 1}},
            {"name": "Blue caps", "pbrMetallicRoughness": {
                "baseColorFactor": [0.15, 0.48, 0.72, 1], "metallicFactor": 0, "roughnessFactor": 1}},
        ],
        "textures": [{"sampler": 0, "source": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"name": "8x8 checker", "bufferView": png_view, "mimeType": "image/png"}],
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(binary)}],
    }
    json_bytes = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_bytes += b" " * (-len(json_bytes) % 4)
    binary.extend(b"\x00" * (-len(binary) % 4))
    total_length = 12 + 8 + len(json_bytes) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, total_length)
            + struct.pack("<I4s", len(json_bytes), b"JSON") + json_bytes
            + struct.pack("<I4s", len(binary), b"BIN\x00") + binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets" / "test.glb")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    data = build_glb()
    args.output.write_bytes(data)
    print(f"Generated {args.output} ({len(data):,} bytes)")


if __name__ == "__main__":
    main()
