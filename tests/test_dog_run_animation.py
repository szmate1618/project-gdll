"""Appending motion must preserve the approved appearance, rig and old clips."""
import copy
import unittest

import numpy as np

from src.dog_run_animation import append_run, rig_fingerprint
from src.glb import pack_glb, read_glb
from src.zombie_lod import accessor_array


class DogRunTests(unittest.TestCase):
    def setUp(self):
        self.document = {
            'asset': {'version': '2.0'}, 'buffers': [{'byteLength': 8}],
            'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': 8}],
            'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 2, 'type': 'SCALAR'}],
            'nodes': [{'name': 'root', 'children': [1]}, {'name': 'hips', 'translation': [0, .5, 0]}],
            'skins': [{'joints': [0, 1]}],
            'animations': [{'name': 'walk', 'channels': [], 'samplers': []}],
            'meshes': [{'name': 'approved mesh'}], 'materials': [{'name': 'approved atlas'}],
        }
        self.binary = np.array([0, 1], dtype='<f4').tobytes()
        self.source = pack_glb(self.document, self.binary)
        self.motion = {'schema_version': 1, 'rig_sha256': rig_fingerprint(self.document),
                       'reference_speed_mps': 3,
                       'times': [0, .3, .6], 'tracks': {
                           'hips': {'rotation': [[0, 0, 0, 1]]*3,
                                    'translation': [[0, .5, 0], [0, .6, 0], [0, .5, 0]]}}}

    def test_preserves_existing_data_and_targets_the_correct_joint(self):
        document, binary = read_glb(append_run(self.source, self.motion))
        for key in ('nodes', 'skins', 'meshes', 'materials'):
            self.assertEqual(document[key], self.document[key])
        self.assertEqual(document['animations'][:-1], self.document['animations'])
        self.assertEqual(binary[:len(self.binary)], self.binary)
        self.assertEqual(document['accessors'][:1], self.document['accessors'])
        run = document['animations'][-1]
        self.assertEqual(run['name'], 'run')
        channel = next(c for c in run['channels'] if c['target']['path'] == 'translation')
        self.assertEqual(channel['target']['node'], 1)
        sampler = run['samplers'][channel['sampler']]
        positions = accessor_array(document, binary, sampler['output'])
        np.testing.assert_allclose(positions[:, 1], [.5, .6, .5])
        np.testing.assert_allclose(accessor_array(document, binary, sampler['input']).ravel(), [0, .3, .6])

    def test_rejects_different_rig_and_duplicate_clip(self):
        motion = copy.deepcopy(self.motion)
        motion['rig_sha256'] = 'different'
        with self.assertRaisesRegex(ValueError, 'rig'):
            append_run(self.source, motion)
        result = append_run(self.source, self.motion)
        with self.assertRaisesRegex(ValueError, 'already'):
            append_run(result, self.motion)

    def test_rejects_invalid_timing_loop_and_world_motion(self):
        variants = []
        bad = copy.deepcopy(self.motion); bad['times'][1] = 0; variants.append(bad)
        bad = copy.deepcopy(self.motion); bad['times'][1] = float('nan'); variants.append(bad)
        bad = copy.deepcopy(self.motion); bad['tracks']['hips']['translation'][-1][1] = .7; variants.append(bad)
        bad = copy.deepcopy(self.motion); bad['tracks']['hips']['rotation'][1] = [0, 0, 0, 2]; variants.append(bad)
        bad = copy.deepcopy(self.motion); bad['tracks']['root'] = {'translation': [[0, 0, 0]]*3}; variants.append(bad)
        bad = copy.deepcopy(self.motion); bad['tracks']['hips']['translation'][1][0] = .2; variants.append(bad)
        bad = copy.deepcopy(self.motion); bad['reference_speed_mps'] = 6; variants.append(bad)
        for bad in variants:
            with self.subTest(motion=bad), self.assertRaises(ValueError):
                append_run(self.source, bad)


if __name__ == '__main__':
    unittest.main()
