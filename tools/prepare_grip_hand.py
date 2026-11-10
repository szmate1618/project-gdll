"""Bake and trim Drillimpact's CC0 right-hand grab pose with Blender.

The normal build reads the tracked JSON; only rebuilding that source needs
Blender and the original creator's GLB. Run with --background --factory-startup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector


SOURCE_SHA256 = "407d6be3cfee18d8d47318d307a6e759f94ed3a1c27e2ab30dd860862b94172b"
POSE = "grab.R"
FRAME = 12
WRIST_LENGTH = .025


def frame(wrist, index, pinky, middle):
    y = (middle - wrist).normalized()
    x = pinky - index
    x = (x - y * x.dot(y)).normalized()
    z = x.cross(y).normalized()
    return Matrix((x, y, z)).transposed()


def surface_contact(mesh):
    """Intersect the palm heel, not the fist's forward knuckles or fingertips."""
    start = Vector((0, .028, 1))
    direction = Vector((0, 0, -1))
    mesh.calc_loop_triangles()
    candidates = []
    for triangle in mesh.loop_triangles:
        a, b, c = (mesh.vertices[i].co for i in triangle.vertices)
        u, v = b - a, c - a
        p = direction.cross(v)
        determinant = u.dot(p)
        if abs(determinant) < 1e-10:
            continue
        inverse = 1 / determinant
        t = start - a
        s = t.dot(p) * inverse
        q = t.cross(u)
        r = direction.dot(q) * inverse
        distance = v.dot(q) * inverse
        if s >= -1e-6 and r >= -1e-6 and s + r <= 1 + 1e-6 and distance > 0:
            point = start + direction * distance
            if u.cross(v).normalized().z > .4:
                candidates.append(point)
    if not candidates:
        raise ValueError("The baked pose has no forward-facing palm heel")
    return max(candidates, key=lambda p: p.z)


def prepare(source):
    if hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError("Unexpected arms_rig.glb source; see the asset README")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.context.scene.render.fps = 30
    bpy.ops.import_scene.gltf(filepath=str(source.resolve()))
    rig, obj = bpy.data.objects["ArmsRig"], bpy.data.objects["ArmsMesh"]
    bones = rig.data.bones
    rest_origin = rig.matrix_world @ bones["hand.R"].head_local
    rest_basis = frame(rest_origin,
        rig.matrix_world @ bones["f_index.01.R"].head_local,
        rig.matrix_world @ bones["f_pinky.01.R"].head_local,
        rig.matrix_world @ bones["f_middle.01.R"].head_local)
    action = bpy.data.actions[POSE]
    rig.animation_data.use_nla = False
    rig.animation_data.action = action
    rig.animation_data.action_slot = action.slots[0]
    bpy.context.scene.frame_set(FRAME)
    bpy.context.view_layer.update()
    posed_bones = rig.pose.bones
    origin = rig.matrix_world @ posed_bones["hand.R"].head
    basis = frame(origin,
        rig.matrix_world @ posed_bones["f_index.01.R"].head,
        rig.matrix_world @ posed_bones["f_pinky.01.R"].head,
        rig.matrix_world @ posed_bones["f_middle.01.R"].head)
    # Curling fingers identify the palm side independently of imported bone axes.
    tip = rig.matrix_world @ posed_bones["f_middle.03.R"].tail
    knuckle = rig.matrix_world @ posed_bones["f_middle.01.R"].head
    if (tip - knuckle).dot(basis.col[2]) < 0:
        basis.col[0] = -basis.col[0]
        basis.col[2] = -basis.col[2]
    inverse_pose, inverse_rest = basis.transposed(), rest_basis.transposed()
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    posed_mesh = evaluated.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    positions = bm.verts.layers.float_vector.new("baked_position")
    right = set()
    for vertex in obj.data.vertices:
        if sum(g.weight for g in vertex.groups
               if obj.vertex_groups[g.group].name.endswith(".R")) > .5:
            right.add(vertex.index)
        bm.verts[vertex.index][positions] = inverse_pose @ (obj.matrix_world @ posed_mesh.vertices[vertex.index].co - origin)
        bm.verts[vertex.index].co = inverse_rest @ (obj.matrix_world @ vertex.co - rest_origin)
    evaluated.to_mesh_clear()
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in right], context="VERTS")
    # glTF duplicates seam vertices for UVs; join them before capping the cut.
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
    # Cut in the undeformed frame so a bent elbow cannot survive as an island.
    cut = bmesh.ops.bisect_plane(bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
        plane_co=Vector((0, -WRIST_LENGTH, 0)), plane_no=Vector((0, 1, 0)),
        clear_inner=True, clear_outer=False, dist=1e-7)
    boundary = [edge for edge in cut["geom_cut"] if isinstance(edge, bmesh.types.BMEdge) and edge.is_boundary]
    for vertex in bm.verts:
        vertex.co = vertex[positions]
    if boundary:
        bmesh.ops.holes_fill(bm, edges=boundary)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    if any(not edge.is_manifold for edge in bm.edges):
        raise ValueError("Trimmed grip hand must have a closed wrist cap")
    mesh = bpy.data.meshes.new("SetGripRightHand")
    bm.to_mesh(mesh)
    bm.free()
    contact = surface_contact(mesh)
    mesh.calc_loop_triangles()
    points = [[round(float(c), 8) for c in vertex.co - contact] for vertex in mesh.vertices]
    triangles = [list(triangle.vertices) for triangle in mesh.loop_triangles]
    if len(triangles) >= 588:
        raise ValueError("Forearm trim failed to reduce the source right arm")
    result = dict(version=1, id="grip_hand", units="meters",
        axes="X across palm, Y toward knuckles, Z into held object",
        copyright="Drillimpact, CC0 1.0; baked and trimmed for Project GDLL",
        source_sha256=SOURCE_SHA256, source_pose=POSE, source_frame=FRAME,
        retained_wrist_m=WRIST_LENGTH, contact_point=[0, 0, 0], contact_direction=[0, 0, 1],
        alpha=.5, parts=[dict(name="SetGripRightHand", color=[.05, .9, .18],
                             positions=points, triangles=triangles)])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    result = prepare(args.source)
    text = json.dumps(result, indent=2, allow_nan=False)
    text = re.sub(r"\[\s*[-+0-9.eE]+(?:,\s*[-+0-9.eE]+)*\s*\]",
                  lambda m: json.dumps(json.loads(m.group())), text)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".tmp")
    temporary.write_text(text + "\n")
    temporary.replace(args.output)
    print("Set grip:", sum(len(p["triangles"]) for p in result["parts"]), "triangles")


if __name__ == "__main__":
    main()
