"""Tongue flattening keeps the sloping top, closed surface and skinning intact."""
import io
import unittest

import numpy as np
from PIL import Image

from src.glb import pack_glb, read_glb
from src.undead_dog_geometry import flatten_tongue
from src.zombie_lod import accessor_array


def fixture():
    # Two pentagonal cross sections with an inclined top y = .8 - .3*z.
    ring = [(-.03, 0), (.03, 0), (.045, -.04), (0, -.07), (-.045, -.04)]
    points = np.array([(x, .8-.3*z+depth, z) for z in (0, .2) for x, depth in ring])
    triangles = []
    for i in range(5):
        j = (i+1) % 5
        triangles += [(i, i+5, j+5), (i, j+5, j)]
    for i in range(1, 4):
        triangles += [(0, i, i+1), (5, i+6, i+5)]
    surface = points[np.array(triangles)]
    outsiders = np.array([[[-1, 0, 0], [-1, 1, 0], [-1, 0, 1]]])
    surface = np.concatenate((surface, outsiders))
    normals = np.cross(surface[:, 1]-surface[:, 0], surface[:, 2]-surface[:, 0])
    normals /= np.linalg.norm(normals, axis=1, keepdims=True)
    positions = surface.reshape(-1, 3).astype('<f4')
    normals = np.repeat(normals, 3, axis=0).astype('<f4')
    count = len(positions)
    document = {'asset': {'version': '2.0'}, 'buffers': [], 'bufferViews': [], 'accessors': []}
    binary = bytearray()

    def add(array, kind, component=5126):
        binary.extend(b'\0' * (-len(binary) % 4))
        view = len(document['bufferViews'])
        document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': array.nbytes})
        binary.extend(array.tobytes())
        index = len(document['accessors'])
        document['accessors'].append({'bufferView': view, 'componentType': component, 'type': kind, 'count': len(array)})
        return index

    uv = np.tile([.35, .85], (count, 1)).astype('<f4')
    uv[-3:] = [.1, .1]
    attrs = {'POSITION': add(positions, 'VEC3'), 'NORMAL': add(normals, 'VEC3'),
             'TEXCOORD_0': add(uv, 'VEC2'),
             'JOINTS_0': add(np.zeros((count, 4), dtype='u1'), 'VEC4', 5121),
             'WEIGHTS_0': add(np.tile([1, 0, 0, 0], (count, 1)).astype('<f4'), 'VEC4')}
    indices = add(np.arange(count, dtype='<u2').reshape(-1, 1), 'SCALAR', 5123)
    bind = add(np.eye(4, dtype='<f4').reshape(1, 16), 'MAT4')
    time = add(np.array([[0], [1]], dtype='<f4'), 'SCALAR')
    translation = add(np.array([[0, 0, 0], [0, .01, 0]], dtype='<f4'), 'VEC3')
    stream = io.BytesIO()
    Image.new('RGB', (1, 1), (113, 52, 59)).save(stream, format='PNG')
    png = stream.getvalue()
    binary.extend(b'\0' * (-len(binary) % 4))
    image_view = len(document['bufferViews'])
    document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(png)})
    binary.extend(png)
    document.update({
        'buffers': [{'byteLength': len(binary)}],
        'nodes': [{'name': 'jaw'}, {'name': 'dog', 'mesh': 0, 'skin': 0}],
        'meshes': [{'primitives': [{'attributes': attrs, 'indices': indices, 'material': 0}]}],
        'skins': [{'joints': [0], 'inverseBindMatrices': bind}],
        'scenes': [{'nodes': [0, 1]}], 'scene': 0,
        'animations': [{'name': 'idle', 'samplers': [{'input': time, 'output': translation}],
                        'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'translation'}}]}],
        'images': [{'bufferView': image_view, 'mimeType': 'image/png'}],
        'textures': [{'source': 0}],
        'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}],
    })
    return pack_glb(document, bytes(binary))


class UndeadDogGeometryTests(unittest.TestCase):
    def test_half_volume_top_fixed_and_closed_surface(self):
        source = fixture()
        result, stats = flatten_tongue(source)
        before, original = read_glb(source)
        after, binary = read_glb(result)
        attrs = before['meshes'][0]['primitives'][0]['attributes']
        old = accessor_array(before, original, attrs['POSITION'])
        new = accessor_array(after, binary, attrs['POSITION'])
        top = np.isclose(old[:, 1]+.3*old[:, 2], .8)
        np.testing.assert_array_equal(new[top], old[top])
        np.testing.assert_array_equal(new[:, [0, 2]], old[:, [0, 2]])
        np.testing.assert_array_equal(new[-3:], old[-3:])
        self.assertTrue(np.all(new[:-3, 1][~top[:-3]] > old[:-3, 1][~top[:-3]]))
        faces = np.arange(48).reshape(-1, 3)

        def volume(p):
            a, b, c = p[faces].transpose(1, 0, 2).astype(float)
            return np.einsum('ij,ij->', a, np.cross(b, c))/6

        self.assertAlmostEqual(volume(new)/volume(old), .5, places=5)
        for positions in (old, new):
            unique, inverse = np.unique(positions[:48], axis=0, return_inverse=True)
            self.assertEqual(len(unique), 10)
            f = inverse.reshape(-1, 3)
            edges = np.sort(np.concatenate((f[:, [0, 1]], f[:, [1, 2]], f[:, [2, 0]])), axis=1)
            self.assertTrue(np.all(np.unique(edges, axis=0, return_counts=True)[1] == 2))
        normals = accessor_array(after, binary, attrs['NORMAL'])[:48].reshape(-1, 3, 3)
        surface = new[:48].reshape(-1, 3, 3)
        geometric = np.cross(surface[:, 1]-surface[:, 0], surface[:, 2]-surface[:, 0])
        geometric /= np.linalg.norm(geometric, axis=1, keepdims=True)
        np.testing.assert_allclose(normals, np.repeat(geometric[:, None, :], 3, axis=1), atol=1e-6)
        self.assertEqual(stats['tongue_thickness_scale'], .5)

    def test_only_positions_and_normals_change(self):
        source = fixture()
        result, _ = flatten_tongue(source)
        before, original = read_glb(source)
        after, binary = read_glb(result)
        for key in ('nodes', 'meshes', 'skins', 'animations', 'materials', 'textures', 'images'):
            self.assertEqual(before[key], after[key])
        for i in range(2, len(before['accessors'])):
            np.testing.assert_array_equal(accessor_array(before, original, i), accessor_array(after, binary, i))
        image = before['bufferViews'][before['images'][0]['bufferView']]
        start, length = image['byteOffset'], image['byteLength']
        self.assertEqual(original[start:start+length], binary[start:start+length])
        self.assertEqual(flatten_tongue(source)[0], result)


if __name__ == '__main__':
    unittest.main()
