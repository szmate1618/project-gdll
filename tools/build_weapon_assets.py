"""Build camera-local GLBs from the tracked Durango weapon mesh source, offline."""
import argparse
import json
import math
from pathlib import Path
import struct


WEAPON_IDS = ("pistol", "submachine_gun", "sniper", "shotgun")


def build_model(weapon):
    binary = bytearray()
    views, accessors, meshes, nodes, materials = [], [], [], [], []

    def vectors(values, bounds=False):
        binary.extend(b"\0" * (-len(binary) % 4))
        data = struct.pack("<" + "f" * (len(values) * 3), *(x for v in values for x in v))
        views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(data), target=34962))
        binary.extend(data)
        accessor = dict(bufferView=len(views)-1, componentType=5126, count=len(values), type="VEC3")
        if bounds:
            accessor.update(min=[min(v[i] for v in values) for i in range(3)],
                            max=[max(v[i] for v in values) for i in range(3)])
        accessors.append(accessor)
        return len(accessors)-1

    for part in weapon["parts"]:
        source, positions, normals = part["positions"], [], []
        if not all(len(p) == 3 and all(math.isfinite(x) for x in p) for p in source):
            raise ValueError("Invalid positions: " + part["name"])
        color = part["color"]
        if len(color) != 3 or not all(math.isfinite(c) and 0 <= c <= 1 for c in color):
            raise ValueError("Invalid material: " + part["name"])
        for triangle in part["triangles"]:
            if len(triangle) != 3 or any(type(i) is not int or not 0 <= i < len(source) for i in triangle):
                raise ValueError("Invalid triangle: " + part["name"])
            a, b, c = (source[i] for i in triangle)
            u, v = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
            n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
            length = math.sqrt(sum(x*x for x in n))
            if length < 1e-12:
                raise ValueError("Degenerate triangle: " + part["name"])
            positions.extend([a, b, c])
            normals.extend([[x/length for x in n]] * 3)
        if not positions:
            raise ValueError("Empty part: " + part["name"])
        attributes = dict(POSITION=vectors(positions, True), NORMAL=vectors(normals))
        material = len(materials)
        materials.append(dict(name=part["name"], pbrMetallicRoughness=dict(
            baseColorFactor=color + [1], metallicFactor=0, roughnessFactor=1)))
        nodes.append(dict(name=part["name"], mesh=len(meshes)))
        meshes.append(dict(name=part["name"], primitives=[dict(attributes=attributes, material=material)]))
    if not meshes:
        raise ValueError("Weapon has no meshes")
    document = dict(asset=dict(version="2.0", generator="GDL Durango weapon builder", copyright="chilly_durango, CC0 1.0"),
                    scene=0, scenes=[dict(nodes=list(range(len(nodes))))], nodes=nodes, meshes=meshes,
                    materials=materials, buffers=[dict(byteLength=len(binary))],
                    bufferViews=views, accessors=accessors)
    text = json.dumps(document, separators=(",", ":"), allow_nan=False).encode()
    text += b" " * (-len(text) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    size = 12 + 8 + len(text) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, size) + struct.pack("<I4s", len(text), b"JSON") + text
            + struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    document = json.loads(args.source.read_text())
    if document["version"] != 1 or document["units"] != "meters" or document["axes"] != "X right, Y up, muzzle -Z":
        raise ValueError("Unsupported weapon mesh format")
    if [w["id"] for w in document["weapons"]] != list(WEAPON_IDS):
        raise ValueError("Weapon slot order must be pistol, SMG, sniper, shotgun")
    # Validate and build all four before publishing any outputs.
    outputs = {w["id"]: build_model(w) for w in document["weapons"]}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        target = args.output_dir / (name + ".glb")
        temporary = target.with_suffix(".tmp.glb")
        temporary.write_bytes(data)
        temporary.replace(target)


if __name__ == "__main__":
    main()
