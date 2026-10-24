"""Offline static vehicle LoDs. All outputs share one chassis-local frame.

Keep authored originals untouched. Bake scene transforms once, join by material,
then simplify each level independently from the selected near mesh.
"""
from __future__ import annotations
import copy
import hashlib
import io
import math
import numpy as np
from PIL import Image
from .glb import read_glb
from .vehicle_assets import mesh_transforms, simplify_vehicle, triangle_count, validated_glb
from .zombie_lod import accessor_array, numeric_attribute

LEVELS = ((1., 1024), (.65, 512), (.35, 256), (.15, 128))


def append_array(document, binary, array, kind, component=5126):
    array = np.asarray(array, dtype='<u4' if component == 5125 else '<f4')
    binary.extend(b'\0' * (-len(binary) % 4))
    view = len(document['bufferViews'])
    document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': array.nbytes})
    binary.extend(array.tobytes())
    spec = {'bufferView': view, 'componentType': component, 'count': len(array), 'type': kind}
    if kind == 'VEC3':
        spec.update(min=array.min(0).tolist(), max=array.max(0).tolist())
    document['accessors'].append(spec)
    return len(document['accessors']) - 1


def chassis_model(document, source_binary, yaw_degrees, length=3.6, bottom=-.68):
    """Fit static complete car to its reference length: Y up, -Z forward, meters.

    Yaw is an explicitly reviewed input, never guessed from an AABB. The same
    uniform fit is shared by every level, including independently simplified ones.
    """
    if not math.isfinite(length) or length <= 0 or not math.isfinite(bottom) or not math.isfinite(yaw_degrees):
        raise ValueError('Invalid vehicle fit')
    if document.get('skins') or document.get('animations'):
        raise ValueError('Expected a static vehicle')
    angle = math.radians(yaw_degrees)
    rotation = np.array([[math.cos(angle), 0, math.sin(angle)], [0, 1, 0],
                         [-math.sin(angle), 0, math.cos(angle)]])
    groups = {}
    points = []
    for mesh, transforms in mesh_transforms(document).items():
        for primitive in document['meshes'][mesh]['primitives']:
            if primitive.get('mode', 4) != 4:
                raise ValueError('Expected triangles')
            attrs = primitive['attributes']
            positions = numeric_attribute(document, source_binary, attrs['POSITION'])
            indices = accessor_array(document, source_binary, primitive['indices']).reshape((-1, 3)) if 'indices' in primitive else np.arange(len(positions)).reshape((-1, 3))
            for transform in transforms:
                linear = rotation @ transform[:3, :3]
                p = positions @ linear.T + rotation @ transform[:3, 3]
                faces = indices[:, ::-1] if np.linalg.det(linear) < 0 else indices
                if 'NORMAL' in attrs:
                    normals = numeric_attribute(document, source_binary, attrs['NORMAL']) @ np.linalg.inv(linear)
                else:
                    import trimesh
                    normals = trimesh.Trimesh(vertices=p, faces=faces, process=False).vertex_normals
                normals = normals / np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-10)
                uv = numeric_attribute(document, source_binary, attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else np.zeros((len(p), 2))
                color = numeric_attribute(document, source_binary, attrs['COLOR_0']) if 'COLOR_0' in attrs else np.ones((len(p), 4))
                if color.shape[1] == 3:
                    color = np.column_stack((color, np.ones(len(p))))
                groups.setdefault(primitive.get('material', -1), []).append((p, normals, uv, color, faces))
                points.append(p)
    all_points = np.concatenate(points)
    lo, hi = all_points.min(0), all_points.max(0)
    if not np.isfinite(all_points).all() or hi[2] - lo[2] <= 0:
        raise ValueError('Invalid vehicle bounds')
    scale = length / (hi[2] - lo[2])
    offset = -(lo + hi) / 2
    offset[1] = -lo[1] + bottom / scale
    result = copy.deepcopy(document)
    binary = bytearray(source_binary)
    result['nodes'] = [{'mesh': 0, 'name': 'chassis-local static vehicle'}]
    result['scenes'] = [{'nodes': [0]}]
    result['scene'] = 0
    primitives = []
    for material, parts in groups.items():
        arrays = [[] for _ in range(5)]
        count = 0
        for p, normals, uv, color, faces in parts:
            for out, value in zip(arrays, ((p + offset) * scale, normals, uv, color, faces + count)):
                out.append(value)
            count += len(p)
        arrays = [np.concatenate(a) for a in arrays]
        attrs = {name: append_array(result, binary, a, kind) for name, a, kind in
                 zip(('POSITION', 'NORMAL', 'TEXCOORD_0', 'COLOR_0'), arrays[:4], ('VEC3', 'VEC3', 'VEC2', 'VEC4'))}
        p = {'attributes': attrs, 'indices': append_array(result, binary, arrays[4].ravel(), 'SCALAR', 5125)}
        if material >= 0:
            p['material'] = material
        primitives.append(p)
    result['meshes'] = [{'primitives': primitives}]
    result['buffers'] = [{'byteLength': len(binary)}]
    # Remove extensions that refer to the original nodes/meshes (e.g. lights).
    result.pop('extensions', None)
    # The runtime shades diffuse/base color only. Convert legacy diffuse input
    # explicitly; never relax the model loader's required-extension validation.
    legacy = 'KHR_materials_pbrSpecularGlossiness'
    for material in result.get('materials', []):
        extension = material.get('extensions', {}).pop(legacy, None)
        if extension is not None:
            pbr = material.setdefault('pbrMetallicRoughness', {})
            pbr['baseColorFactor'] = extension.get('diffuseFactor', [1, 1, 1, 1])
            if 'diffuseTexture' in extension:
                pbr['baseColorTexture'] = extension['diffuseTexture']
            pbr['metallicFactor'] = 0
            pbr['roughnessFactor'] = 1 - extension.get('glossinessFactor', 1)
    for key in ('extensionsUsed', 'extensionsRequired'):
        if key in result:
            result[key] = [value for value in result[key] if value != legacy]
    result.setdefault('extras', {})['vehicle_frame'] = {'yaw_degrees': yaw_degrees, 'length_m': length, 'bottom_m': bottom,
                                                       'dimensions': ((hi-lo)*scale).tolist()}
    packed = validated_glb(result, bytes(binary))
    return read_glb(packed)


def resize_textures(document, source_binary, maximum):
    result = copy.deepcopy(document)
    binary = bytearray(source_binary)
    for spec in result.get('images', []):
        view = document['bufferViews'][spec['bufferView']]
        start = view.get('byteOffset', 0)
        with Image.open(io.BytesIO(source_binary[start:start + view['byteLength']])) as image:
            if max(image.size) <= maximum:
                continue
            image = image.convert('RGBA')
            image.thumbnail((maximum, maximum), Image.Resampling.LANCZOS)
            encoded = io.BytesIO()
            image.save(encoded, format='PNG', optimize=True)
        binary.extend(b'\0' * (-len(binary) % 4))
        spec['bufferView'] = len(result['bufferViews'])
        spec['mimeType'] = 'image/png'
        result['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(encoded.getvalue())})
        binary.extend(encoded.getvalue())
    result['buffers'][0]['byteLength'] = len(binary)
    return result, bytes(binary)


def texture_inventory(document, binary):
    """Exact RGBA8 allocation including every mip level, per glTF texture object.

    The current uploader creates one allocation per texture, even when two
    texture objects reference the same image. Encoded size is separate.
    """
    images = []
    for spec in document.get('images', []):
        view = document['bufferViews'][spec['bufferView']]
        start = view.get('byteOffset', 0)
        data = binary[start:start + view['byteLength']]
        with Image.open(io.BytesIO(data)) as image:
            width, height = image.size
        w, h = width, height
        gpu = 0
        while True:
            gpu += w * h * 4
            if w == h == 1:
                break
            w, h = max(1, w // 2), max(1, h // 2)
        images.append({'width': width, 'height': height, 'encoded_bytes': len(data),
                       'rgba8_mip_bytes': gpu, 'sha256': hashlib.sha256(data).hexdigest()})
    textures = document.get('textures', [])
    return {'images': images, 'texture_objects': len(textures),
            'gpu_bytes': sum(images[t['source']]['rgba8_mip_bytes'] for t in textures)}


def generate_levels(document, binary, helper):
    count = triangle_count(document)
    floor = sum(min(6, document['accessors'][p['indices']]['count'] // 3)
                for m in document['meshes'] for p in m['primitives'])
    levels = []
    for level, (ratio, texture_size) in enumerate(LEVELS):
        target = min(count, max(48, floor, int(count * ratio)))
        # Preserve material borders and UV discontinuities. Quality wins
        # over the requested count; LoDs must not tear a texture atlas apart.
        reduced, data, _ = simplify_vehicle(document, binary, helper, maximum=target, strict_budget=False, lock_borders=True,
                                             error_limit=(0., .01, .025, .04)[level])
        reduced, data = resize_textures(reduced, data, texture_size)
        packed = validated_glb(reduced, data)
        doc, data = read_glb(packed)
        levels.append((packed, {'triangles': triangle_count(doc), 'texture_limit': texture_size,
                                'textures': texture_inventory(doc, data)}))
    return levels
