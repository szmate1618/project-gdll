"""Shape profiles preserve the animated proxy and change only intended corners."""
from collections import Counter
import hashlib
from pathlib import Path
import unittest

import numpy as np

from src.glb import read_glb
from src.zombie_lod import accessor_array
from src.zombie_prism_lod import generate_prism_lod, production_prism_color_mode


SOURCE = Path(__file__).resolve().parents[1] / 'assets/zombies/models/ZombieMale_C.glb'
# Hashes include all five clips; geometry and rig preservation are checked below.
PRODUCTION_HASHES = {
    'ZombieFemale_A': 'f29ec01e07b1eaf7a4412f5eb6e85b7db84d166f5763a8cad8367094c03ad6fb',
    'ZombieFemale_B': '1b159506d30113f774bd2a220f2f96fcf552ffd3bf338a73b5dddc268e6648a8',
    'ZombieFemale_C': '4f087327fe12f98c7847088cfeed9051c39f667a63e1a4588479472463a0b551',
    'ZombieFemale_D': '992e402724143a7e64578e9938373b6651025bc5f17b75a6fe4fdd0ee257ff45',
    'ZombieFemale_E': '2009f00d2c520a65fb254c401f7879918f89dab9d5cc944fbc2e63d3847d733d',
    'ZombieMale_A': 'a74a03cf201b134e2d2a9d9761f572f9f586dda49cb7e572ba65dd3789de31b4',
    'ZombieMale_B': 'dbd5b24fbbd20b2fad32a7435e75e79535ad585a78bd79ce3fab1c2f381b4ec2',
    'ZombieMale_C': '2064ee8fd96fb70cf73c2392924ef9fbe7da219b455d5fe8c527dda4e6659638',
    'ZombieMale_D': '491266a6557979a60086857b48ae42bdd41a550460cf8d4c6eb068b3b693452b',
    'ZombieMale_E': 'f2c96e08e9cec71c9bc70b7383c597008784becae31fbd25ee9cb1bd2dedf1d0',
}


@unittest.skipUnless(SOURCE.is_file(), 'Cached ZombieMale_C source asset is unavailable')
class ZombiePrismLodTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_bytes()
        cls.original, cls.original_binary = read_glb(cls.source)
        cls.models = {}
        for color in ('sampled', 'white_vest'):
            for profile in ('standard', 'slim', 'slim2'):
                output, metadata = generate_prism_lod(cls.source, color_mode=color, profile=profile)
                document, binary = read_glb(output)
                primitive = document['meshes'][0]['primitives'][0]
                arrays = {name: accessor_array(document, binary, index)
                          for name, index in primitive['attributes'].items()}
                arrays['indices'] = accessor_array(document, binary, primitive['indices']).reshape(-1, 3)
                cls.models[color, profile] = (output, metadata, document, binary, arrays)

    def test_standard_and_slim_profiles_reproduce_five_clip_assets(self):
        hashes = {
            ('sampled', 'standard'): '628965ad441a16cae30f05823c742374090084c568554c922732df325ee58c60',
            ('white_vest', 'standard'): 'cf0c124603541920fb374d5aea5f8739b6321e383cfd35a1562061c7eb1ca9d3',
            ('sampled', 'slim'): '81aeae02c33000ea5bae02688a7cc29a6f7720179added127e5c4a196ea58b9e',
            ('white_vest', 'slim'): '2064ee8fd96fb70cf73c2392924ef9fbe7da219b455d5fe8c527dda4e6659638',
            ('sampled', 'slim2'): 'bf2a4b56297268b254bb3985bb6b076bfa9e22a8c197e8f954d84d9a4b1e41bb',
            ('white_vest', 'slim2'): 'ebef11428dc2249a284f4ac77021aa3cded78c6655a4a9ab2ea86267bae3bf4b',
        }
        for key, expected in hashes.items():
            self.assertEqual(hashlib.sha256(self.models[key][0]).hexdigest(), expected)
        for color in ('sampled', 'white_vest'):
            legacy, _ = generate_prism_lod(self.source, color_mode=color, slim=True)
            self.assertEqual(legacy, self.models[color, 'slim'][0])
        default, _ = generate_prism_lod(self.source)
        self.assertEqual(default, self.models['white_vest', 'standard'][0])

    def test_slim_has_smaller_torso_and_tips_with_fixed_proximal_geometry(self):
        standard = self.models['sampled', 'standard']
        slim = self.models['sampled', 'slim']
        old, new = standard[4]['POSITION'], slim[4]['POSITION']
        changed = np.any(old != new, axis=1)
        self.assertEqual(int(changed.sum()), 18)
        inverse_bind = accessor_array(self.original, self.original_binary,
                                     self.original['skins'][0]['inverseBindMatrices'])
        bind = np.linalg.inv(inverse_bind.reshape(-1, 4, 4).transpose(0, 2, 1))
        joint_names = {self.original['nodes'][node]['name']: index
                       for index, node in enumerate(self.original['skins'][0]['joints'])}
        expected_changed = np.zeros(60, dtype=bool)
        for part in standard[1]['parts']:
            first = part['first_vertex']
            if part['name'] == 'torso':
                expected_changed[first:first + 6] = True
                axis = np.asarray(part['axis'])
                for ring in range(2):
                    section = slice(first + ring * 3, first + ring * 3 + 3)
                    center = np.asarray(part['rings'][ring]['center'])
                    # Longitudinal coordinates stay fixed while every diameter
                    # of the triangular cross-section shrinks by 15 percent.
                    np.testing.assert_allclose((new[section] - center) @ axis,
                                               (old[section] - center) @ axis, atol=1e-5)
                    for a, b in ((0, 1), (1, 2), (2, 0)):
                        before = np.linalg.norm(old[section][a] - old[section][b])
                        after = np.linalg.norm(new[section][a] - new[section][b])
                        self.assertAlmostEqual(after / before, .85, places=6)
            elif part['name'].endswith(('_lower_arm', '_lower_leg')):
                expected_changed[first + 3:first + 6] = True
                anchor = bind[joint_names['bip ' + part['end_joint']], :3, 3]
                before, after = old[first + 3:first + 6] - anchor, new[first + 3:first + 6] - anchor
                np.testing.assert_allclose(after, before * .7, atol=1e-5)
        np.testing.assert_array_equal(changed, expected_changed)
        np.testing.assert_array_equal(new[~expected_changed], old[~expected_changed])

    def test_slim2_narrows_and_lowers_only_the_two_rear_torso_corners(self):
        for color in ('sampled', 'white_vest'):
            slim = self.models[color, 'slim']
            slim2 = self.models[color, 'slim2']
            old, new = slim[4]['POSITION'], slim2[4]['POSITION']
            torso = next(part for part in slim[1]['parts'] if part['name'] == 'torso')
            first = torso['first_vertex']
            rear = [first + 4, first + 5]
            changed = np.flatnonzero(np.any(old != new, axis=1))
            np.testing.assert_array_equal(changed, rear)
            unchanged = np.ones(60, dtype=bool)
            unchanged[rear] = False
            np.testing.assert_array_equal(new[unchanged], old[unchanged])
            axis = np.asarray(torso['axis'])
            height = np.dot(np.asarray(torso['rings'][1]['center']) - torso['rings'][0]['center'], axis)
            before, after = old[rear].astype(np.float64), new[rear].astype(np.float64)
            np.testing.assert_allclose(after.mean(0) - before.mean(0), -.1 * height * axis, atol=1e-5)
            np.testing.assert_allclose(after[0] - after[1], .85 * (before[0] - before[1]), atol=1e-5)
            self.assertAlmostEqual(np.linalg.norm(after[0] - after[1]) /
                                   np.linalg.norm(before[0] - before[1]), .85, places=6)
            self.assertTrue(np.any(slim2[4]['NORMAL'][first:first + 6] !=
                                   slim[4]['NORMAL'][first:first + 6]))

    def test_profiles_preserve_colors_rig_and_open_topology(self):
        for (color, profile), (_, _, document, binary, arrays) in self.models.items():
            for name in ('nodes', 'scenes', 'scene', 'skins', 'animations'):
                self.assertEqual(document.get(name), self.original.get(name))
            self.assertEqual(binary[:len(self.original_binary)], self.original_binary)
            for attribute in ('COLOR_0', 'JOINTS_0', 'WEIGHTS_0', 'indices'):
                np.testing.assert_array_equal(arrays[attribute], self.models[color, 'standard'][4][attribute])
            self.assertEqual(arrays['POSITION'].shape, (60, 3))
            self.assertEqual(arrays['indices'].shape, (60, 3))
            np.testing.assert_allclose(np.linalg.norm(arrays['NORMAL'], axis=1), 1, atol=1e-6)
            edges = Counter()
            for triangle in arrays['indices']:
                self.assertEqual(len(set(triangle // 6)), 1)
                self.assertEqual(len(set((triangle % 6) // 3)), 2, 'Triangle must span both open rings')
                for a, b in zip(triangle, np.roll(triangle, -1)):
                    edges[tuple(sorted((int(a), int(b))))] += 1
            self.assertEqual(Counter(edges.values()), {1: 60, 2: 60})
            boundary = {edge for edge, count in edges.items() if count == 1}
            expected_rims = {tuple(sorted((first + a, first + b))) for first in range(0, 60, 3)
                             for a, b in ((0, 1), (1, 2), (2, 0))}
            self.assertEqual(boundary, expected_rims, 'Exactly twenty open triangular rims')
        for profile in ('standard', 'slim', 'slim2'):
            for attribute in ('POSITION', 'NORMAL', 'JOINTS_0', 'WEIGHTS_0', 'indices'):
                np.testing.assert_array_equal(self.models['sampled', profile][4][attribute],
                                              self.models['white_vest', profile][4][attribute])


@unittest.skipUnless(all((SOURCE.parent / (name + '.glb')).is_file() for name in PRODUCTION_HASHES),
                     'All ten cached zombie source assets are required')
class ProductionZombiePrismTests(unittest.TestCase):
    def test_all_types_preserve_rigs_colors_and_slim_geometry(self):
        for name, expected_hash in PRODUCTION_HASHES.items():
            with self.subTest(model=name):
                path = SOURCE.parent / (name + '.glb')
                source = path.read_bytes()
                original, original_binary = read_glb(source)
                mode = production_prism_color_mode(path)
                self.assertEqual(mode, 'white_vest' if name == 'ZombieMale_C' else 'sampled')
                standard, standard_metadata = generate_prism_lod(source, color_mode=mode, profile='standard')
                output, metadata = generate_prism_lod(source, color_mode=mode, profile='slim')
                self.assertEqual(hashlib.sha256(output).hexdigest(), expected_hash)
                self.assertEqual(metadata['source_sha256'], hashlib.sha256(source).hexdigest())
                document, binary = read_glb(output)
                standard_document, standard_binary = read_glb(standard)
                self.assertEqual(len(document['meshes']), 1)
                self.assertEqual(len(document['meshes'][0]['primitives']), 1)
                self.assertEqual(binary[:len(original_binary)], original_binary)
                for key in ('nodes', 'scenes', 'scene', 'skins', 'animations'):
                    self.assertEqual(document.get(key), original.get(key))
                for key in ('accessors', 'bufferViews', 'materials'):
                    self.assertEqual(document[key][:len(original[key])], original[key])
                primitive = document['meshes'][0]['primitives'][0]
                standard_primitive = standard_document['meshes'][0]['primitives'][0]
                arrays = {attribute: accessor_array(document, binary, index)
                          for attribute, index in primitive['attributes'].items()}
                old = {attribute: accessor_array(standard_document, standard_binary, index)
                       for attribute, index in standard_primitive['attributes'].items()}
                for attribute, array in arrays.items():
                    self.assertTrue(np.all(np.isfinite(array)), attribute)
                np.testing.assert_allclose(np.linalg.norm(arrays['NORMAL'], axis=1), 1, atol=1e-6)
                np.testing.assert_allclose(arrays['WEIGHTS_0'].sum(axis=1), 1, atol=1e-6)
                self.assertLess(int(arrays['JOINTS_0'].max()), len(original['skins'][0]['joints']))
                self.assertTrue(np.all((arrays['COLOR_0'] >= 0) & (arrays['COLOR_0'] <= 1)))
                for attribute in ('COLOR_0', 'JOINTS_0', 'WEIGHTS_0'):
                    np.testing.assert_array_equal(arrays[attribute], old[attribute])
                positions, previous = arrays['POSITION'], old['POSITION']
                # Separate float32 exports can round opposite ways. Allow two
                # coordinate ULPs (under one micrometer in these cm-scale rigs).
                coordinate_tolerance = float(2 * np.spacing(np.max(np.abs(previous))))
                self.assertEqual(positions.shape, (60, 3))
                self.assertEqual(metadata['topology'], {'components': 10, 'boundary_edges': 60,
                                  'triangles': 60, 'vertices': 60, 'caps': 0, 'outward_winding': True})
                self.assertEqual(int(np.any(positions != previous, axis=1).sum()), 18)
                bind = np.linalg.inv(accessor_array(original, original_binary,
                    original['skins'][0]['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1))
                names = {original['nodes'][node]['name']: index
                         for index, node in enumerate(original['skins'][0]['joints'])}
                for part in standard_metadata['parts']:
                    first = part['first_vertex']
                    if part['name'] == 'torso':
                        axis = np.asarray(part['axis'])
                        for ring in range(2):
                            section = slice(first + ring * 3, first + ring * 3 + 3)
                            center = np.asarray(part['rings'][ring]['center'])
                            np.testing.assert_allclose((positions[section] - center) @ axis,
                                (previous[section] - center) @ axis, atol=coordinate_tolerance)
                            for a, b in ((0, 1), (1, 2), (2, 0)):
                                self.assertAlmostEqual(np.linalg.norm(positions[section][a] - positions[section][b]) /
                                    np.linalg.norm(previous[section][a] - previous[section][b]), .85, places=6)
                    elif part['name'].endswith(('_lower_arm', '_lower_leg')):
                        np.testing.assert_array_equal(positions[first:first + 3], previous[first:first + 3])
                        anchor = bind[names['bip ' + part['end_joint']], :3, 3]
                        np.testing.assert_allclose(positions[first + 3:first + 6] - anchor,
                            .7 * (previous[first + 3:first + 6] - anchor), atol=coordinate_tolerance)
                    else:
                        np.testing.assert_array_equal(positions[first:first + 6], previous[first:first + 6])


if __name__ == '__main__':
    unittest.main()
