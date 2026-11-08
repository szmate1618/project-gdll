import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from shapely.geometry import Polygon, box
from shapely.ops import unary_union

from police_station_fixture import write_fixture
from src.config import load_config
from src.glb import read_glb
from src.police_station_floorplan import definition, reference_feature, station_frame, prepare_layout
from src.police_station_scene_update import prepare_station_patch
from src.verify import inspect_glb


class PoliceStationFloorplanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = load_config(Path(__file__).resolve().parents[1] / 'config.json')
        cls.feature = reference_feature(cls.config)
        cls.frame = station_frame(cls.feature, cls.feature['geometry'], cls.config)
        cls.plan = prepare_layout(cls.frame, 0)

    def test_courtyard_floor_regions_tile_exact_mapped_shell(self):
        outline = self.frame[3]
        self.assertEqual(len(outline.interiors), 5)
        regions = [Polygon(p) for p in self.plan['floor_regions']]
        self.assertLess(unary_union(regions).symmetric_difference(outline).area, 1e-8)
        self.assertAlmostEqual(sum(p.area for p in regions), outline.area)
        for storey in range(4):
            rooms = [Polygon(r['polygon']) for r in self.plan['rooms'] if r['storey'] == storey]
            self.assertTrue(all(outline.buffer(1e-7).covers(p) for p in rooms))
            self.assertAlmostEqual(sum(p.area for p in rooms), unary_union(rooms).area)
            self.assertLess(outline.difference(unary_union(rooms)).area, outline.area * .005)

    def test_two_stair_cores_connect_all_four_storeys_with_clear_landings(self):
        self.assertEqual(len(self.plan['stairs']), 6)
        for stair in self.plan['stairs']:
            x0, y0, x1, y1 = stair['ramp']
            room = self.plan['rooms'][stair['room']]
            self.assertTrue(Polygon(room['polygon']).covers(box(x0, y0 - .6, x1, y1 + .6)))
            self.assertEqual(stair['top'], self.plan['upper_floors'][room['storey']])
        for i in (0, 1, 2, 3):
            self.assertTrue(box(*self.plan['stairs'][i]['ramp']).disjoint(box(*self.plan['stairs'][i+2]['ramp'])))

    def test_override_matches_only_this_complete_building(self):
        self.assertIsNone(station_frame({**self.feature, 'id': 'way/another'}, self.feature['geometry'], {}))
        with self.assertRaises(ValueError):
            station_frame(self.feature, Polygon(self.feature['geometry'].exterior), self.config)

    def test_export_and_patch_preserve_closed_solids_and_other_buildings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'station.glb'
            stats = write_fixture(path, legacy_canopy=True)
            self.assertEqual(stats['doors'], 4)
            self.assertGreater(stats['windows'], 100)
            self.assertTrue(inspect_glb(path)['valid'])
            original = path.read_bytes()
            before, binary_before = read_glb(original)
            patched, summary = prepare_station_patch(original, lambda x, y: np.zeros(np.broadcast(x, y).shape), self.config)
            after, binary_after = read_glb(patched)
            self.assertEqual(binary_after[:len(binary_before)], binary_before)
            unchanged = lambda doc: [m for m in doc['meshes'] if not m.get('name', '').startswith(
                ('building_relation_3897212_0_', 'building_way_1564681267_0_'))]
            self.assertEqual(unchanged(before), unchanged(after))
            self.assertTrue(any(m.get('name') == 'building_way_1564681267_0_walls' for m in before['meshes']))
            self.assertFalse(any(m.get('name', '').startswith('building_way_1564681267_0_') for m in after['meshes']))
            self.assertTrue(any(m.get('name') == 'building_relation_3897212_0_canopy' for m in after['meshes']))
            self.assertEqual(summary['storeys'], 4)
            wall = next(m for m in after['meshes'] if m['name'] == 'building_relation_3897212_0_walls')
            self.assertEqual(wall['extras']['authored_floorplan']['version'], 2)
            self.assertEqual(len(wall['extras']['authored_floorplan']['courtyards']), 5)
            path.write_bytes(patched)
            self.assertTrue(inspect_glb(path)['valid'])


if __name__ == '__main__':
    unittest.main()
