"""LOD output must retain animation, skinning and original vertex attributes."""
import copy
from pathlib import Path
import struct
import unittest
import numpy as np
from src.glb import pack_glb, read_glb
from src.zombie_lod import accessor_array, generate_lod

HELPER = Path(__file__).resolve().parents[1] / 'build/zombie_mesh_simplify'


def fixture():
    document = {'asset': {'version': '2.0'}, 'buffers': [{'byteLength': 0}],
                'bufferViews': [], 'accessors': [], 'meshes': [],
                'nodes': [{'name': 'Body', 'mesh': 0, 'skin': 0}, {'name': 'Hip', 'children': [2]},
                          {'name': 'Head'}], 'scenes': [{'nodes': [0, 1]}], 'scene': 0}
    binary = bytearray()

    def add(array, shape, component=5126):
        binary.extend(b'\0' * (-len(binary) % 4))
        view = len(document['bufferViews'])
        document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': array.nbytes})
        binary.extend(array.tobytes())
        document['accessors'].append({'bufferView': view, 'count': len(array), 'componentType': component, 'type': shape})
        return len(document['accessors']) - 1

    positions = np.asarray([(x, y, 0) for y in range(9) for x in range(9)], dtype='<f4')
    positions_index = add(positions, 'VEC3')
    normals_index = add(np.tile(np.asarray([0, 0, 1], dtype='<f4'), (81, 1)), 'VEC3')
    joints = np.zeros((81, 4), dtype='<u2')
    joints[positions[:, 1] > 4, 0] = 1
    joint_index = add(joints, 'VEC4', 5123)
    skin = np.zeros((81, 4), dtype='<f4'); skin[:, 0] = 1
    weight_index = add(skin, 'VEC4')
    indices = []
    for y in range(8):
        for x in range(8):
            a = y * 9 + x
            indices.extend([a, a + 1, a + 9, a + 1, a + 10, a + 9])
    index = add(np.asarray(indices, dtype='<u2')[:, None], 'SCALAR', 5123)
    document['meshes'] = [{'primitives': [{'attributes': {'POSITION': positions_index, 'NORMAL': normals_index,
                                'JOINTS_0': joint_index, 'WEIGHTS_0': weight_index}, 'indices': index}]}]
    bind = add(np.tile(np.eye(4, dtype='<f4').reshape(1, 16), (2, 1)), 'MAT4')
    document['skins'] = [{'joints': [1, 2], 'skeleton': 1, 'inverseBindMatrices': bind}]
    time = add(np.asarray([[0], [1]], dtype='<f4'), 'SCALAR')
    move = add(np.asarray([[0, 1, 0], [0, 2, 0]], dtype='<f4'), 'VEC3')
    document['animations'] = [{'name': 'idle', 'samplers': [{'input': time, 'output': move}],
                               'channels': [{'sampler': 0, 'target': {'node': 2, 'path': 'translation'}}]}]
    document['buffers'][0]['byteLength'] = len(binary)
    return pack_glb(document, bytes(binary))


class ZombieLodTests(unittest.TestCase):
    @unittest.skipUnless(HELPER.is_file(), 'Build zombie_mesh_simplify first')
    def test_smaller_mesh_preserves_rig_animation_and_skin_attributes(self):
        source = fixture()
        original, old_binary = read_glb(source)
        for ratio in (0.5, 0.2, 0.07):
            output, stats = generate_lod(source, HELPER, ratio, 0.16)
            reduced, binary = read_glb(output)
            self.assertLess(stats['triangles'], 128)
            self.assertLess(stats['vertices'], 81)
            self.assertEqual(binary[:len(old_binary)], old_binary)
            for name in ('nodes', 'scenes', 'skins', 'animations'):
                self.assertEqual(reduced[name], original[name])
            self.assertEqual(reduced['accessors'][:len(original['accessors'])], original['accessors'])
            original_attrs = original['meshes'][0]['primitives'][0]['attributes']
            attrs = reduced['meshes'][0]['primitives'][0]['attributes']
            old_positions = accessor_array(original, old_binary, original_attrs['POSITION'])
            positions = accessor_array(reduced, binary, attrs['POSITION'])
            for vertex, position in enumerate(positions):
                source_vertex = np.flatnonzero(np.all(old_positions == position, axis=1))[0]
                for name in attrs:
                    np.testing.assert_array_equal(accessor_array(reduced, binary, attrs[name])[vertex],
                                                   accessor_array(original, old_binary, original_attrs[name])[source_vertex])
            indices = accessor_array(reduced, binary, reduced['meshes'][0]['primitives'][0]['indices'])
            self.assertLess(int(indices.max()), len(positions))

    def test_interleaved_offsets_and_truncated_views(self):
        binary = struct.pack('<8f', 99, 1, 2, 3, 88, 4, 5, 6)
        document = {'bufferViews': [{'buffer': 0, 'byteLength': 32, 'byteStride': 16}],
                    'accessors': [{'bufferView': 0, 'byteOffset': 4, 'count': 2, 'type': 'VEC3', 'componentType': 5126}]}
        np.testing.assert_array_equal(accessor_array(document, binary, 0), [[1, 2, 3], [4, 5, 6]])
        bad = copy.deepcopy(document); bad['bufferViews'][0]['byteLength'] = 28
        with self.assertRaises(ValueError): accessor_array(bad, binary, 0)


if __name__ == '__main__':
    unittest.main()
