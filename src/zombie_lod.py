"""Rig-preserving offline zombie mesh simplification.

Only mesh attribute/index accessors are replaced. Original nodes, skins, animation
accessors and their binary bytes remain unchanged, including centimeter conversion.
"""
from __future__ import annotations
import copy
import hashlib
import struct
import subprocess
import numpy as np
from .glb import read_glb, pack_glb

DTYPES = {5120: '<i1', 5121: '<u1', 5122: '<i2', 5123: '<u2', 5125: '<u4', 5126: '<f4'}
WIDTHS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
LEVELS = ((0.5, 0.03), (0.2, 0.08), (0.07, 0.16))


def accessor_array(document, binary, index):
    accessor = document['accessors'][index]
    if 'sparse' in accessor or 'bufferView' not in accessor:
        raise ValueError('LOD preparation requires dense accessors')
    view = document['bufferViews'][accessor['bufferView']]
    dtype = np.dtype(DTYPES[accessor['componentType']])
    width, count = WIDTHS[accessor['type']], accessor['count']
    packed = width * dtype.itemsize
    stride = view.get('byteStride', packed)
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    length = (count - 1) * stride + packed if count else 0
    if view['buffer'] != 0 or stride < packed or offset + length > len(binary) or \
            accessor.get('byteOffset', 0) + length > view['byteLength']:
        raise ValueError('Invalid LOD accessor bounds')
    return np.ndarray((count, width), dtype=dtype, buffer=binary,
                      offset=offset, strides=(stride, dtype.itemsize)).copy()


def numeric_attribute(document, binary, index):
    result = accessor_array(document, binary, index).astype(np.float32)
    accessor = document['accessors'][index]
    if accessor.get('normalized'):
        dtype = np.dtype(DTYPES[accessor['componentType']])
        result = np.maximum(result / np.iinfo(dtype).max, -1)
    return result


def simplify_primitive(document, binary, primitive, helper, ratio, error):
    if primitive.get('mode', 4) != 4 or 'targets' in primitive:
        raise ValueError('LOD preparation supports triangle meshes without morph targets')
    attributes = primitive['attributes']
    positions = numeric_attribute(document, binary, attributes['POSITION'])
    indices = (accessor_array(document, binary, primitive['indices']).ravel() if 'indices' in primitive
               else np.arange(len(positions), dtype=np.uint32))
    if len(indices) % 3 or np.max(indices) >= len(positions):
        raise ValueError('Invalid triangle indices')
    values, weights = [], []
    for name, weight in (('NORMAL', 0.15), ('TEXCOORD_0', 2.0), ('COLOR_0', 0.5)):
        if name in attributes:
            data = numeric_attribute(document, binary, attributes[name])
            values.append(data)
            weights.extend([weight] * data.shape[1])
    # Compare influence weights in joint space, never joint IDs as continuous
    # numbers. The most represented joints get the remaining attribute budget.
    influences = {}
    for suffix in ('0', '1'):
        if 'JOINTS_' + suffix in attributes:
            joints = accessor_array(document, binary, attributes['JOINTS_' + suffix])
            skin = numeric_attribute(document, binary, attributes['WEIGHTS_' + suffix])
            for joint in np.unique(joints):
                contribution = np.sum(np.where(joints == joint, skin, 0), axis=1)
                influences[int(joint)] = influences.get(int(joint), 0) + contribution
    for joint in sorted(influences, key=lambda j: (-float(influences[j].sum()), j))[:32-len(weights)]:
        values.append(influences[joint][:, None])
        weights.append(0.4)
    if not values:
        values, weights = [np.zeros((len(positions), 1), dtype=np.float32)], [0.0]
    values = np.concatenate(values, axis=1)
    target = max(3, int(len(indices) * ratio) // 3 * 3)
    payload = (struct.pack('<4If', len(positions), len(indices), len(weights), target, error) +
               positions.astype('<f4').tobytes() + values.astype('<f4').tobytes() +
               np.asarray(weights, dtype='<f4').tobytes() + indices.astype('<u4').tobytes())
    result = subprocess.run([str(helper)], input=payload, capture_output=True, check=True)
    reduced = np.frombuffer(result.stdout, dtype='<u4')
    if len(reduced) < 3 or len(reduced) % 3 or np.max(reduced) >= len(positions) or len(reduced) > len(indices):
        raise ValueError('Invalid simplifier output')
    # Preserve first-use order for vertex fetch locality.
    remap = {}
    used = []
    compact = []
    for vertex in reduced:
        key = int(vertex)
        if key not in remap:
            remap[key] = len(used)
            used.append(key)
        compact.append(remap[key])
    return np.asarray(used), np.asarray(compact, dtype='<u4')


def generate_lod(source, helper, ratio, error):
    document, original_binary = read_glb(source)
    document = copy.deepcopy(document)
    binary = bytearray(original_binary)

    def append_accessor(template, array, target):
        binary.extend(b'\0' * (-len(binary) % 4))
        view_index = len(document['bufferViews'])
        document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary),
                                      'byteLength': array.nbytes, 'target': target})
        binary.extend(array.tobytes())
        accessor = copy.deepcopy(template)
        accessor.pop('byteOffset', None)
        accessor.pop('sparse', None)
        accessor['bufferView'], accessor['count'] = view_index, len(array)
        if 'min' in accessor or 'max' in accessor:
            accessor['min'], accessor['max'] = array.min(axis=0).tolist(), array.max(axis=0).tolist()
        document['accessors'].append(accessor)
        return len(document['accessors']) - 1

    triangles = vertices = 0
    for mesh in document['meshes']:
        for primitive in mesh['primitives']:
            used, indices = simplify_primitive(document, original_binary, primitive, helper, ratio, error)
            for name, index in list(primitive['attributes'].items()):
                array = accessor_array(document, original_binary, index)[used]
                primitive['attributes'][name] = append_accessor(document['accessors'][index], array, 34962)
            primitive['indices'] = append_accessor({'type': 'SCALAR', 'componentType': 5125},
                                                   indices[:, None], 34963)
            triangles += len(indices) // 3
            vertices += len(used)
    document['buffers'][0]['byteLength'] = len(binary)
    document.setdefault('extras', {})['zombie_lod'] = {
        'version': 1, 'source_sha256': hashlib.sha256(source).hexdigest(),
        'triangle_ratio': ratio, 'meshoptimizer': '1.3'}
    result = pack_glb(document, bytes(binary))
    read_glb(result)  # Validate before publication.
    return result, {'triangles': triangles, 'vertices': vertices}
