"""Extract the CC0 Durango meshes and make the scoped M14 variant in Blender.

Run with Blender, not the project Python interpreter; see assets/weapons/README.md.
Only this offline preparation step needs the downloaded Guns.blend source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import bmesh
import bpy
from mathutils import Vector


SOURCE_SHA256 = "f12a0cbdb38568908d9dba3ed4529de6257d6ba2739026fc16de15195533e93f"
STEEL = [0.24, 0.27, 0.29]
DARK = [0.10, 0.12, 0.13]
WOOD = [0.32, 0.17, 0.075]


def extract(prefix, length):
    objects = sorted((o for o in bpy.data.objects
                      if o.type == "MESH" and o.name.startswith(prefix + "_")),
                     key=lambda o: o.name)
    if not objects:
        raise ValueError("Missing source weapon: " + prefix)
    coords = [o.matrix_world @ v.co for o in objects for v in o.data.vertices]
    center_x = (min(v.x for v in coords) + max(v.x for v in coords)) / 2
    front, rear = min(v.y for v in coords), max(v.y for v in coords)
    center_y = (front + rear) / 2
    scale = length / (rear - front)
    parts = []
    for obj in objects:
        mesh = obj.data.copy()
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(mesh)
        bm.free()
        mesh.calc_loop_triangles()
        # Blender source is Z-up with muzzle toward -Y. This proper rotation
        # makes camera-local meters: X right, Y up, muzzle toward -Z.
        positions = []
        for vertex in mesh.vertices:
            p = obj.matrix_world @ vertex.co
            positions.append([round(-(p.x - center_x) * scale, 7),
                              round(p.z * scale, 7),
                              round((p.y - center_y) * scale, 7)])
        color = DARK if any(s in obj.name for s in ("Magazine", "Pump")) else STEEL
        if prefix == "M14" and obj.name.endswith("Body"):
            color = WOOD
        triangles = []
        for triangle in mesh.loop_triangles:
            a, b, c = (Vector(positions[i]) for i in triangle.vertices)
            # The M14 fore-end includes a collinear n-gon corner. Blender's
            # tessellation emits one zero-area sliver after float rounding;
            # remove that empty triangle during preparation, not at runtime.
            if (b-a).cross(c-a).length < 1e-12:
                print("Removing zero-area source triangle:", obj.name, list(triangle.vertices))
                continue
            triangles.append(list(triangle.vertices))
        parts.append(dict(name=obj.name, color=color, positions=positions, triangles=triangles))
        bpy.data.meshes.remove(mesh)
    return parts


def box(name, center, size, color):
    x, y, z = center
    a, b, c = (s / 2 for s in size)
    positions = [[x-a, y-b, z-c], [x+a, y-b, z-c], [x+a, y+b, z-c], [x-a, y+b, z-c],
                 [x-a, y-b, z+c], [x+a, y-b, z+c], [x+a, y+b, z+c], [x-a, y+b, z+c]]
    faces = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 4, 7, 3],
             [1, 2, 6, 5], [3, 7, 6, 2], [0, 1, 5, 4]]
    return dict(name=name, color=color, positions=positions,
                triangles=[t for a, b, c, d in faces for t in ([a, b, c], [a, c, d])])


def cylinder(name, radius, depth, center, color):
    # Six sides, axis Z: the scope and barrel keep the source's angular style.
    import math
    x, y, z = center
    positions = [[x + radius * math.cos(i * math.tau / 6),
                  y + radius * math.sin(i * math.tau / 6), z + end * depth / 2]
                 for end in (-1, 1) for i in range(6)]
    triangles = []
    for i in range(6):
        j = (i + 1) % 6
        triangles.extend([[i, j, j+6], [i, j+6, i+6]])
    for i in range(1, 5):
        triangles.extend([[0, i+1, i], [6, i+6, i+7]])
    return dict(name=name, color=color, positions=positions, triangles=triangles)


def sniper():
    parts = extract("M14", 1.0)
    # Extend only the exposed fore-end/barrel vertices; preserve receiver,
    # trigger, magazine and stock proportions. Muzzle ends at Z=-0.66 m.
    for part in parts:
        for position in part["positions"]:
            if position[2] < -0.25:
                position[2] = round(-0.25 + (position[2] + 0.25) * 1.64, 7)
    parts.extend([
        cylinder("Sniper_HeavyBarrel", .015, .35, (0, .014, -.485), STEEL),
        box("Sniper_FrontMount", (0, .063, -.08), (.035, .055, .032), DARK),
        box("Sniper_RearMount", (0, .063, .09), (.035, .055, .032), DARK),
        cylinder("Sniper_ScopeTube", .026, .24, (0, .102, -.01), DARK),
        cylinder("Sniper_Objective", .040, .072, (0, .102, -.166), DARK),
        cylinder("Sniper_Eyepiece", .031, .050, (0, .102, .135), DARK),
        cylinder("Sniper_ObjectiveGlass", .034, .002, (0, .102, -.203), [.06, .20, .23]),
        cylinder("Sniper_EyepieceGlass", .026, .002, (0, .102, .161), [.06, .20, .23]),
    ])
    return parts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    source = Path(bpy.data.filepath)
    if hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError("Unexpected Guns.blend source; check the documented download")
    weapons = [dict(id="pistol", source="1911", parts=extract("1911", .22)),
               dict(id="submachine_gun", source="UMP", parts=extract("UMP", .65)),
               dict(id="sniper", source="M14, modified", parts=sniper()),
               dict(id="shotgun", source="TacticalPump", parts=extract("TacticalPump", .95))]
    document = dict(version=1, units="meters", axes="X right, Y up, muzzle -Z",
                    source_sha256=SOURCE_SHA256, weapons=weapons)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".tmp")
    text = json.dumps(document, indent=2, allow_nan=False)
    # One vertex/face per line keeps the source geometry reviewable.
    text = re.sub(r"\[\s*[-+0-9.eE]+(?:,\s*[-+0-9.eE]+)*\s*\]",
                  lambda m: json.dumps(json.loads(m.group())), text)
    temporary.write_text(text + "\n")
    temporary.replace(args.output)
    for weapon in weapons:
        print(weapon["id"], sum(len(p["triangles"]) for p in weapon["parts"]), "triangles")


if __name__ == "__main__":
    main()
