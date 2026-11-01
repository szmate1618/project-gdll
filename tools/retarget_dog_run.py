"""Blender 4.5: transfer the CC0 PSX dog run to the approved 23-joint dog rig.

Run with --background --factory-startup --disable-autoexec --python this_file.
All solving uses Blender world meters (Z up, forward -Y). Saved tracks use the
original glTF joint-local axes (Y up). Meshes, bind matrices and old clips are
never exported through Blender; the asset builder appends only these tracks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import zipfile

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.glb import read_glb
from src.dog_run_animation import rig_fingerprint

ASSETS = ROOT / 'assets/undead_dog'
BODY_MAP = {'hips': 'spine3', 'spine': 'spine2', 'chest': 'spine',
            'neck': 'hals', 'head': 'head', 'tail1': 'tail1', 'tail2': 'tail2'}
# The donor has lateral shoulder/hip bones and extra lower-leg segments. Use
# evaluated paw trajectories and a two-link IK solve, rather than dropping
# rotations from those intermediate bones.
LEGS = [('f', 'arm_{side}.001', 'arm2_{side}', 'hand_{side}'),
        ('h', 'fuss2_{side}', 'fuss2_{side}.001', 'fuss4_{side}')]


def local_matrix(node):
    q = node.get('rotation', [0, 0, 0, 1])
    return Matrix.LocRotScale(Vector(node.get('translation', [0, 0, 0])),
                              Quaternion((q[3], *q[:3])), Vector(node.get('scale', [1, 1, 1])))


def solve_knee(hip, paw, pole, upper_length, lower_length):
    direction = paw - hip
    distance = max(1e-5, min(direction.length, (upper_length + lower_length) * .9999))
    direction.normalize()
    paw = hip + direction * distance
    bend = pole - hip
    bend -= direction * bend.dot(direction)
    if bend.length < 1e-6:
        bend = Vector((0, 1, 0))
        bend -= direction * bend.dot(direction)
    bend.normalize()
    along = (upper_length**2 - lower_length**2 + distance**2) / (2 * distance)
    height = math.sqrt(max(0, upper_length**2 - along**2))
    return hip + direction * along + bend * height, paw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=ASSETS / 'source/PSX_Dog_Run_CC0.zip')
    parser.add_argument('--target', type=Path, default=ASSETS / 'models/undead_dog_variant_1.glb')
    parser.add_argument('--output', type=Path, default=ASSETS / 'animations/run.json')
    parser.add_argument('--blend', type=Path, default=ASSETS / 'models/undead_dog_run_edit.blend')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    provenance = json.loads((ASSETS / 'animations/run-source.json').read_text())
    archive = args.archive.read_bytes()
    if hashlib.sha256(archive).hexdigest() != provenance['archive_sha256']:
        raise ValueError('Run donor archive differs from the recorded CC0 source')
    scratch = ROOT / 'build/dog-run-check'
    scratch.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.archive) as bundle:
        fbx = bundle.read(provenance['fbx_member'])
    if hashlib.sha256(fbx).hexdigest() != provenance['fbx_sha256']:
        raise ValueError('Run donor FBX fingerprint mismatch')
    donor_path = scratch / 'donor.fbx'
    donor_path.write_bytes(fbx)
    document, _ = read_glb(args.target.read_bytes())
    nodes = document['nodes']
    names = {node['name']: i for i, node in enumerate(nodes)}
    parents = {child: i for i, n in enumerate(nodes) for child in n.get('children', [])}
    rest = {}
    conversion = Matrix.Rotation(math.pi / 2, 4, 'X')

    def world(index):
        if index not in rest:
            rest[index] = (world(parents[index]) if index in parents else conversion) @ local_matrix(nodes[index])
        return rest[index]

    for index in range(len(nodes)):
        world(index)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(donor_path), use_anim=True)
    source = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    action = source.animation_data.action
    first, last = action.frame_range
    scene = bpy.context.scene
    source_duration = (last - first) / (scene.render.fps / scene.render.fps_base)
    count = round(source_duration * 60)
    frames = [first + (last - first) * i / count for i in range(count + 1)]
    source_rest = {b.name: source.matrix_world @ b.matrix_local for b in source.data.bones}
    samples = []
    for frame in frames:
        scene.frame_set(int(frame), subframe=frame % 1)
        samples.append({b.name: source.matrix_world @ b.matrix for b in source.pose.bones})
    # Normalize the body bob around the approved height; remove horizontal root
    # travel so simulation remains the sole owner of world movement.
    bob_center = sum(s['spine3'].translation.z for s in samples[:-1]) / count
    target_length = (rest[names['chest']].translation - rest[names['hips']].translation).length
    source_length = (source_rest['spine'].translation - source_rest['spine3'].translation).length
    scale = target_length / source_length
    poses = []
    feet_samples = []
    for sample in samples:
        pose = {i: m.copy() for i, m in rest.items()}

        def set_rotation(name, rotation):
            i = names[name]
            parent = pose[parents[i]]
            position = parent @ Vector(nodes[i].get('translation', [0, 0, 0]))
            pose[i] = Matrix.LocRotScale(position, rotation, rest[i].to_scale())

        for target_name, source_name in BODY_MAP.items():
            delta = sample[source_name].to_quaternion() @ source_rest[source_name].to_quaternion().inverted()
            set_rotation(target_name, delta @ rest[names[target_name]].to_quaternion())
            if target_name == 'hips':
                pose[names['hips']].translation.z += (sample['spine3'].translation.z - bob_center) * scale
        current_feet = []
        for prefix, source_hip, source_knee, source_paw in LEGS:
            for side in ('L', 'R'):
                upper, lower, paw = [f'{prefix}_{part}.{side}' for part in ('upper', 'lower', 'paw')]
                sh, sk, sp = [n.format(side=side) for n in (source_hip, source_knee, source_paw)]
                a, b, c = [rest[names[n]].translation for n in (upper, lower, paw)]
                l1, l2 = (b-a).length, (c-b).length
                # Match the donor's foot displacement to this dog's leg length.
                leg_scale = (l1+l2) / (source_rest[sp].translation-source_rest[sh].translation).length
                desired = c + (sample[sp].translation-source_rest[sp].translation) * leg_scale
                desired.z = max(c.z, desired.z)
                upper_pos = pose[parents[names[upper]]] @ Vector(nodes[names[upper]]['translation'])
                pole = upper_pos + (sample[sk].translation-sample[sh].translation) * leg_scale
                knee, desired = solve_knee(upper_pos, desired, pole, l1, l2)
                for name, old_vector, new_vector in ((upper, b-a, knee-upper_pos), (lower, c-b, desired-knee)):
                    set_rotation(name, old_vector.rotation_difference(new_vector) @ rest[names[name]].to_quaternion())
                delta = sample[sp].to_quaternion() @ source_rest[sp].to_quaternion().inverted()
                set_rotation(paw, delta @ rest[names[paw]].to_quaternion())
                current_feet.append(pose[names[paw]].translation.copy())
        # Unmapped jaw and ears follow their parent with their original local pose.
        for name in ('jaw', 'ear.L', 'ear.R'):
            i = names[name]
            pose[i] = pose[parents[i]] @ local_matrix(nodes[i])
        poses.append(pose)
        feet_samples.append(current_feet)
    # Estimate stride speed from low, backward-moving paws, then retime the clip
    # once for the game's 3 m/s chase. This leaves the replicated clock in seconds.
    speeds = []
    dt = source_duration / count
    floors = [min(frame[i].z for frame in feet_samples) for i in range(4)]
    for previous, current in zip(feet_samples, feet_samples[1:]):
        for i, (a, b) in enumerate(zip(previous, current)):
            if max(a.z, b.z) <= floors[i] + .025 and abs(a.z-b.z) < .015 and (b.y-a.y) / dt > .1:
                speeds.append((b.y-a.y) / dt)
    speeds.sort()
    stride_speed = speeds[len(speeds)//2] if speeds else 1.5
    duration = source_duration * stride_speed / 3
    poses[-1] = {i: m.copy() for i, m in poses[0].items()}
    tracks = {}
    for index in document['skins'][0]['joints']:
        name = nodes[index]['name']
        if name in ('root', 'jaw', 'ear.L', 'ear.R'):
            continue
        rotations, translations = [], []
        previous = None
        for pose in poses:
            local = pose[parents[index]].inverted() @ pose[index]
            q = local.to_quaternion().normalized()
            if previous is not None and previous.dot(q) < 0:
                q.negate()
            previous = q.copy()
            rotations.append([q.x, q.y, q.z, q.w])
            translations.append(list(local.translation))
        rotations[-1] = rotations[0][:]
        tracks[name] = {'rotation': rotations}
        if name == 'hips':
            translations[-1] = translations[0][:]
            tracks[name]['translation'] = translations
    motion = {'schema_version': 1, 'rig_sha256': rig_fingerprint(document),
              'source_manifest': 'run-source.json', 'source_duration_seconds': source_duration,
              'reference_speed_mps': 3, 'estimated_source_speed_mps': stride_speed,
              'times': [duration*i/count for i in range(count+1)], 'tracks': tracks}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(motion, separators=(',', ':'), allow_nan=False) + '\n')

    # Save a local editable Blender action on the original rig, with the donor
    # alongside it, for the user's later cosmetic cleanup. Original assets stay
    # untouched; only the append-only asset builder publishes runtime models.
    for o in scene.objects:
        o.hide_set(True)
        o.hide_render = True
    bpy.ops.import_scene.gltf(filepath=str(args.target))
    target = next(o for o in scene.objects if o.type == 'ARMATURE' and o != source)
    target.animation_data_create()
    target.animation_data.action = None
    for track in target.animation_data.nla_tracks:
        track.mute = True
    for frame, pose in enumerate(poses, 1):
        scene.frame_set(frame)
        desired = {bone.name: pose[names[bone.name]] @ rest[names[bone.name]].inverted() @ bone.bone.matrix_local
                   for bone in target.pose.bones}
        for bone in target.pose.bones:
            bone.rotation_mode = 'QUATERNION'
            # Supply the intended parent pose explicitly: reading live parent
            # matrices here can use the previous frame before depsgraph update.
            kwargs = ({'parent_matrix': desired[bone.parent.name],
                       'parent_matrix_local': bone.parent.bone.matrix_local} if bone.parent else {})
            bone.matrix_basis = bone.bone.convert_local_to_pose(
                desired[bone.name], bone.bone.matrix_local, invert=True, **kwargs)
            bone.keyframe_insert('rotation_quaternion', frame=frame, group=bone.name)
            bone.keyframe_insert('location', frame=frame, group=bone.name)
    target.animation_data.action.name = 'run'
    scene.render.fps = 60
    scene.render.fps_base = duration * 60 / count
    scene.frame_start, scene.frame_end = 1, count+1
    scene.frame_set(1)
    args.blend.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print(f'Retargeted {len(tracks)} joints, {count+1} keys, {duration:.3f}s at 3 m/s; {args.output}')


if __name__ == '__main__':
    main()
