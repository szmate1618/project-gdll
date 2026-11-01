"""Import, export and render the cached pigeon in Blender 4.5 (CPU Cycles).

Run from the repository root with --background --factory-startup --disable-autoexec.
The mesh and rig retain the FBX's meter scale; Blender uses Z up. GLB export
converts once to glTF's Y-up frame. Studio objects are excluded from the GLB.
"""
from pathlib import Path
import argparse
import json
import sys

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets/pigeon"


def point_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()


def bounds(obj):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    vertices = [evaluated.matrix_world @ v.co for v in evaluated.data.vertices]
    low = Vector([min(v[i] for v in vertices) for i in range(3)])
    high = Vector([max(v[i] for v in vertices) for i in range(3)])
    return low, high


def set_action(rig, action):
    # FBX imports have separate Blender 4.5 action slots. Assigning the action
    # alone leaves its slot unset, which silently evaluates the rest pose.
    rig.animation_data.action = action
    rig.animation_data.action_slot = action.slots[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--texture-size', type=int, choices=(512, 16), default=512)
    tint = parser.add_mutually_exclusive_group()
    tint.add_argument('--warm-white', action='store_true', help='Use the off-white 16×16 palette atlas')
    tint.add_argument('--yellow-beige', action='store_true', help='Use the darker yellow-beige 16×16 palette atlas')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if (args.warm_white or args.yellow_beige) and args.texture_size != 16:
        parser.error('Feather tint variants require --texture-size 16')
    suffix = '' if args.texture_size == 512 else '_16'
    if args.warm_white:
        suffix += '_warm'
    elif args.yellow_beige:
        suffix += '_yellow'
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=str(ASSETS / "source/pigeon.fbx"))
    scene = bpy.context.scene
    bird = next(o for o in scene.objects if o.type == 'MESH')
    rig = next(o for o in scene.objects if o.type == 'ARMATURE')
    actions = {}
    for action in bpy.data.actions:
        action.name = action.name.split('|')[-1]
        action.use_fake_user = True
        actions[action.name] = action
    if len(bird.data.vertices) != 357 or len(actions) != 5:
        raise ValueError("Expected the original 357-vertex pigeon with five actions")
    bird.data.calc_loop_triangles()
    assert len(bird.data.loop_triangles) == 710
    assert any(m.type == 'ARMATURE' and m.object == rig for m in bird.modifiers)

    atlas = bpy.data.images.load(str(ASSETS / f"models/pigeon_atlas{suffix}.png"), check_existing=False)
    if tuple(atlas.size) != (args.texture_size, args.texture_size):
        raise ValueError('Texture dimensions do not match the requested variant')
    atlas.pack()
    material = bpy.data.materials.new('Pigeon — undead surface palette')
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Roughness'].default_value = .86
    bsdf.inputs['Specular IOR Level'].default_value = .18
    texture = material.node_tree.nodes.new('ShaderNodeTexImage')
    texture.image = atlas
    texture.interpolation = 'Closest'
    material.node_tree.links.new(texture.outputs['Color'], bsdf.inputs['Base Color'])
    bird.data.materials.clear()
    bird.data.materials.append(material)

    set_action(rig, actions['Standing Idle'])
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT')
    bird.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(
        filepath=str(ASSETS / f'models/pigeon_palette{suffix}.glb'), export_format='GLB',
        use_selection=True, export_animations=True, export_animation_mode='ACTIONS',
        export_force_sampling=True, export_frame_range=False,
        export_skins=True, export_yup=True, export_all_influences=True,
    )

    # Keep lighting neutral so the atlas supplies all of the bird's colors.
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 32
    scene.cycles.use_denoising = True
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 4
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    world = bpy.data.worlds.new('Neutral studio')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs[0].default_value = (.36, .39, .43, 1)
    world.node_tree.nodes['Background'].inputs[1].default_value = .45
    scene.world = world

    low, high = bounds(bird)
    ground_z = low.z - .0004
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, ground_z))
    ground = bpy.context.object
    ground.name = 'Studio floor'
    floor = bpy.data.materials.new('Studio charcoal')
    floor.diffuse_color = (.13, .15, .18, 1)
    floor.use_nodes = True
    floor.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (.13, .15, .18, 1)
    floor.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 1
    ground.data.materials.append(floor)
    for name, position, energy, size in [
        ('Key', (.4, -.5, .7), 4, .5),
        ('Fill', (-.4, -.1, .3), 1.4, .4),
        ('Rim', (0, .4, .6), 2.8, .35),
    ]:
        light_data = bpy.data.lights.new(name, 'AREA')
        light_data.energy = energy
        light_data.shape = 'DISK'
        light_data.size = size
        light = bpy.data.objects.new(name, light_data)
        scene.collection.objects.link(light)
        light.location = position
        point_at(light, (0, 0, .12))
    camera_data = bpy.data.cameras.new('Preview camera')
    camera = bpy.data.objects.new('Preview camera', camera_data)
    scene.collection.objects.link(camera)
    camera_data.type = 'ORTHO'
    scene.camera = camera
    shots = [
        ('pigeon_palette', 'Standing Idle', 1, (1, -1.6, .65)),
        ('pigeon_side', 'Standing Idle', 1, (1, -.12, .25)),
        ('pigeon_flight', 'Gliding', 20, (1, -1.5, 1.1)),
    ]
    report = []
    for name, action, frame, direction in shots:
        set_action(rig, actions[action])
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        low, high = bounds(bird)
        center = (low + high) / 2
        radius = (high - low).length / 2
        camera_data.ortho_scale = radius * 2.35
        camera.location = center + Vector(direction).normalized()
        point_at(camera, center)
        ground.hide_render = action != 'Standing Idle'
        scene.render.filepath = str(ASSETS / f'previews/{name}{suffix}.png')
        bpy.ops.render.render(write_still=True)
        report.append({'file': name + suffix + '.png', 'action': action, 'frame': frame})

    set_action(rig, actions['Standing Idle'])
    scene.frame_set(1)
    ground.hide_render = False
    low, high = bounds(bird)
    center = (low + high) / 2
    camera_data.ortho_scale = (high - low).length * 1.175
    camera.location = center + Vector(shots[0][3]).normalized()
    point_at(camera, center)
    bpy.ops.wm.save_as_mainfile(filepath=str(ASSETS / f'models/pigeon_palette{suffix}.blend'))
    report_path = ASSETS / f'previews/render{suffix}.json'
    report_path.write_text(json.dumps({
        'renderer': 'Blender ' + bpy.app.version_string + ' / CPU Cycles',
        'triangles': 710, 'vertices': 357, 'bones': len(rig.data.bones),
        'texture_size': args.texture_size, 'texture_filter': 'Closest (nearest)',
        'warm_white': args.warm_white,
        'yellow_beige': args.yellow_beige,
        'actions': sorted(actions), 'views': report,
        'note': 'Neutral studio lighting; palette is exact in the texture, shading changes rendered pixel colors.',
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
