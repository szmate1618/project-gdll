"""Combined vegetation preserves mapped trunks and suppresses nearby image proposals."""
import json
from copy import deepcopy
from pathlib import Path
import unittest

from src.forest_comparison import file_sha256
from src.tree_combination import combine_trees
from tests.satellite_tree_fixtures import SatelliteTreeFixture


class TreeCombinationTests(SatelliteTreeFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.osm = self.root/'osm.json'
        self.tuned = self.root/'tuned.json'
        self.combined = self.root/'combined/trees.instances.json'
        self.document = {'schema': 'godollo.tree-instances', 'version': 1,
            'map': {'path': self.map_path.name, 'map_sha256': file_sha256(self.map_path),
                    'crs': self.frame['crs'], 'units': 'meters', 'origin_projected_m': list(self.frame['origin'])},
            'assets': {'tree': {'path': self.asset.name, 'sha256': file_sha256(self.asset)}},
            'transforms': {'matrix_authoritative': True, 'units': 'meters', 'matrix_layout': 'column-major',
                           'axes': {'X': 'east', 'Y': 'up', 'Z': 'south'}}, 'source': {}, 'instances': []}

    @staticmethod
    def instance(identifier, x, z):
        return {'id': identifier, 'asset': 'tree', 'source': {'placement': 'mapped row' if 'row' in identifier else 'point'},
                'matrix': [1., 0, 0, 0, 0, 1., 0, 0, 0, 0, 1., 0, x, 0, z, 1.]}

    def write(self, path, instances):
        document = deepcopy(self.document)
        document['instances'] = instances
        path.write_text(json.dumps(document))

    def test_keeps_osm_points_rows_and_only_distant_tuned2_with_original_transforms(self):
        mapped = [self.instance('osm:node', 0, 0), self.instance('osm:row:1', 40, 0)]
        proposed = [self.instance('near', 5, 0), self.instance('boundary', 6, 0),
                    self.instance('outside', 6.01, 0), self.instance('row-near', 40, 3)]
        proposed[2]['matrix'][8] = .3  # Retain shear instead of reconstructing transforms.
        self.write(self.osm, mapped)
        self.write(self.tuned, proposed)
        before = {p: p.read_bytes() for p in (self.osm, self.tuned)}
        result = combine_trees(self.osm, self.tuned, self.map_path, self.combined)
        combined = json.loads(self.combined.read_text())
        self.assertEqual(combined['instances'], mapped + [proposed[2]])
        self.assertEqual(result['tuned2_suppressed_near_osm'], 3)
        self.assertEqual(result['osm_instances'], 2)
        self.assertEqual((self.combined.parent/combined['assets']['tree']['path']).resolve(), self.asset)
        self.assertEqual((self.combined.parent/combined['map']['path']).resolve(), self.map_path)
        self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_empty_osm_layer_keeps_all_tuned2(self):
        proposals = [self.instance('tuned', 0, 0)]
        self.write(self.osm, [])
        self.write(self.tuned, proposals)
        combine_trees(self.osm, self.tuned, self.map_path, self.combined)
        self.assertEqual(json.loads(self.combined.read_text())['instances'], proposals)

    def test_incompatible_input_preserves_last_good_combination(self):
        self.write(self.osm, [])
        self.write(self.tuned, [self.instance('tuned', 0, 0)])
        combine_trees(self.osm, self.tuned, self.map_path, self.combined)
        previous = self.combined.read_bytes()
        document = json.loads(self.osm.read_text())
        document['map']['map_sha256'] = 'stale'
        self.osm.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, 'published map'):
            combine_trees(self.osm, self.tuned, self.map_path, self.combined)
        self.assertEqual(self.combined.read_bytes(), previous)

    def test_refuses_overwriting_an_input(self):
        self.write(self.osm, [])
        self.write(self.tuned, [])
        with self.assertRaisesRegex(ValueError, 'overwrite an input'):
            combine_trees(self.osm, self.tuned, self.map_path, self.tuned)


if __name__ == '__main__':
    unittest.main()
