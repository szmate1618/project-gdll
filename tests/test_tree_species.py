"""Pine clusters use 10% centers, horizontal metric distances and calibrated matrices."""
from copy import deepcopy
import json
import unittest

import numpy as np

from src.tree_species import assign_pine_clusters


TREE = {'footprint_m': [10., 8.], 'visible_height_m': 10.,
        'geometry_bounds_m': {'min': [-6., -.2, -5.], 'max': [6., 10.5, 5.]}}
PINE = {'footprint_m': [4., 4.], 'visible_height_m': 14.,
        'geometry_bounds_m': {'min': [-3., -.3, -3.], 'max': [3., 14.5, 3.]}}


def layer(count=80):
    instances = []
    for i in range(count):
        # Reflected, rotated and sheared horizontal axes with independent scale.
        matrix = np.array([[-1.2, 0, .4, (i % 10) * 10.],
                           [0, 1.5, 0, i * .1], [.5, 0, 1.7, (i // 10) * 10.], [0, 0, 0, 1]])
        lo, hi = np.array(TREE['geometry_bounds_m']['min']), np.array(TREE['geometry_bounds_m']['max'])
        center = matrix[:3, :3] @ ((lo + hi) / 2) + matrix[:3, 3]
        radius = np.abs(matrix[:3, :3]) @ ((hi - lo) / 2)
        instances.append({'id': f'tree-{i}', 'asset': 'tree', 'matrix': matrix.ravel(order='F').tolist(),
                          'position': matrix[:3, 3].tolist(), 'dimensions_m': [13., 15., 14.],
                          'source': {'row': i},
                          'bounds': {'min': (center - radius).tolist(), 'max': (center + radius).tolist()}})
    return {'assets': {'tree': deepcopy(TREE)}, 'instances': instances, 'source': {},
            'transforms': {'matrix_authoritative': True, 'matrix_layout': 'column-major',
                           'units': 'meters', 'axes': {'X': 'east', 'Y': 'up', 'Z': 'south'}}}


class PineClusterTests(unittest.TestCase):
    def test_ten_percent_centers_and_exact_horizontal_neighborhoods(self):
        document = layer()
        before = deepcopy(document)
        result = assign_pine_clusters(document, PINE)
        centers = document['source']['pine_clusters']['center_ids']
        self.assertEqual(len(centers), 8)  # 10% of 80 trees.
        self.assertEqual(len(set(centers)), 8)
        self.assertEqual(document['source']['pine_clusters']['center_percent'], 10)
        points = {t['id']: np.array(t['matrix'])[[12, 14]] for t in before['instances']}
        expected = {key for key, point in points.items()
                    if any(np.linalg.norm(point - points[c]) <= 15 for c in centers)}
        actual = {t['id'] for t in document['instances'] if t['asset'] == 'pine'}
        self.assertEqual(actual, expected)
        self.assertGreater(len(actual), 8)
        self.assertLess(len(actual), len(points))
        self.assertEqual(result['new_pines'], len(expected))
        for old, new in zip(before['instances'], document['instances']):
            self.assertEqual(old['position'], new['position'])
            self.assertEqual(old['dimensions_m'], new['dimensions_m'])
            self.assertEqual(old['source'], new['source'])
            if old['id'] not in expected:
                self.assertEqual(old, new)
                continue
            a = np.array(old['matrix']).reshape(4, 4, order='F')
            b = np.array(new['matrix']).reshape(4, 4, order='F')
            # Corresponding physical footprint/height axes retain orientation,
            # reflection, shear and size despite different asset calibrations.
            np.testing.assert_allclose(a[:3, :3] * [10, 10, 8], b[:3, :3] * [4, 14, 4])
            self.assertLess(np.linalg.det(b[:3, :3]), 0)
            for x in (-3, 3):
                for y in (-.3, 14.5):
                    for z in (-3, 3):
                        world = (b @ [x, y, z, 1])[:3]
                        self.assertTrue((world >= np.array(new['bounds']['min']) - 1e-9).all())
                        self.assertTrue((world <= np.array(new['bounds']['max']) + 1e-9).all())

    def test_selection_survives_reordering_and_repeat_application(self):
        first, second = layer(), layer()
        second['instances'].reverse()
        assign_pine_clusters(first, PINE)
        assign_pine_clusters(second, PINE)
        self.assertEqual(first['source'], second['source'])
        self.assertEqual({t['id']: t for t in first['instances']}, {t['id']: t for t in second['instances']})
        saved = json.dumps(first, sort_keys=True)
        self.assertEqual(assign_pine_clusters(first, PINE)['new_pines'], 0)
        self.assertEqual(json.dumps(first, sort_keys=True), saved)

    def test_boundary_elevation_and_no_cascading(self):
        document = layer(20)
        for i, tree in enumerate(document['instances']):
            tree['matrix'][12], tree['matrix'][14] = i * 1000., 0
        probe = deepcopy(document)
        assign_pine_clusters(probe, PINE)
        centers = probe['source']['pine_clusters']['center_ids']
        trees = {t['id']: t for t in document['instances']}
        center_x = trees[centers[0]]['matrix'][12]
        outsiders = [t for key, t in trees.items() if key not in centers]
        for tree, distance in zip(outsiders, (15., 15.001, 30.)):
            tree['matrix'][12], tree['matrix'][14] = center_x + distance, 0
        outsiders[0]['matrix'][13] = 500.  # Elevation cannot change horizontal distance.
        assign_pine_clusters(document, PINE)
        converted = {t['id'] for t in document['instances'] if t['asset'] == 'pine'}
        self.assertIn(outsiders[0]['id'], converted)
        self.assertNotIn(outsiders[1]['id'], converted)
        self.assertNotIn(outsiders[2]['id'], converted)

    def test_empty_and_percentage_rounding(self):
        self.assertEqual(assign_pine_clusters(layer(0), PINE)['pine_cluster_centers'], 0)
        for size, expected in ((1, 0), (4, 0), (5, 1), (10, 1), (14, 1), (15, 2), (20, 2), (100, 10)):
            with self.subTest(size=size):
                document = layer(size)
                result = assign_pine_clusters(document, PINE)
                self.assertEqual(result['pine_cluster_centers'], expected)
                if expected == 0:
                    self.assertTrue(all(t['asset'] == 'tree' for t in document['instances']))

    def test_bad_matrix_or_calibration_is_rejected(self):
        document = layer()
        document['instances'][0]['matrix'][12] = float('nan')
        with self.assertRaisesRegex(ValueError, 'matrix'):
            assign_pine_clusters(document, PINE)
        pine = deepcopy(PINE)
        pine['footprint_m'][0] = 0
        with self.assertRaisesRegex(ValueError, 'calibration'):
            assign_pine_clusters(layer(), pine)


if __name__ == '__main__':
    unittest.main()
