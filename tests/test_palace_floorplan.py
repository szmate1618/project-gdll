import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from shapely.geometry import Polygon
from shapely.ops import unary_union

from palace_fixture import feature, write_fixture
from src.config import load_config
from src.glb import read_glb
from src.palace_floorplan import palace_frame, prepare_layout
from src.verify import inspect_glb


class PalaceFloorplanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = load_config(Path(__file__).resolve().parents[1] / 'config.json')
        cls.feature = feature(cls.config)
        cls.frame = palace_frame(cls.feature, cls.feature['geometry'], cls.config)
        cls.layout = prepare_layout(cls.frame, 0)

    def test_storeys_tile_actual_shell_without_courtyard_floors(self):
        outline = self.frame[3]
        for storey in (0, 1):
            rooms = [Polygon(r['polygon']) for r in self.layout['rooms'] if r['storey'] == storey]
            union = unary_union(rooms)
            self.assertLess(outline.symmetric_difference(union).area, outline.area * .005)
            self.assertAlmostEqual(sum(p.area for p in rooms), union.area, places=5)
            self.assertTrue(all(outline.buffer(1e-6).covers(p) for p in rooms))
        labels = {r['label'] for r in self.layout['rooms']}
        self.assertIn('Ceremonial hall', labels)
        self.assertIn('45 - Sisi salon', labels)

    def test_stairs_and_landings_stay_in_shell(self):
        from shapely.geometry import box
        self.assertEqual(len(self.layout['stairs']), 2)
        for stair in self.layout['stairs']:
            x0, y0, x1, y1 = stair['ramp']
            self.assertTrue(self.frame[3].covers(box(x0, y0 - .6, x1, y1 + .6)))
            self.assertGreater(stair['top'] - stair['bottom'], 4)
            self.assertTrue(Polygon(self.layout['rooms'][stair['room']]['polygon']).covers(box(*stair['ramp'])))

    def test_unrelated_buildings_are_not_overridden(self):
        other = {**self.feature, 'id': 'way/other'}
        self.assertIsNone(palace_frame(other, other['geometry'], {}))

    def test_export_preserves_closed_shell_and_embeds_prepared_interior(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'palace.glb'
            write_fixture(path)
            report = inspect_glb(path)
            self.assertEqual(report['building_solids_checked'], 1)
            doc, _ = read_glb(path.read_bytes())
            wall = next(m for m in doc['meshes'] if m['name'].endswith('_0_walls'))
            prepared = wall['extras']['authored_floorplan']
            self.assertEqual(prepared['version'], 1)
            self.assertEqual(prepared['upper_floors'], [4.6])
            self.assertEqual(len(prepared['rooms']), len(self.layout['rooms']))
            import trimesh
            scene = trimesh.load_scene(path, process=False)
            panes = scene.geometry['building_way_107918884_0_windows']
            self.assertEqual(len(np.unique(np.round(panes.vertices[:, 2], 2))), 4)

    def test_existing_map_patch_replaces_only_palace_and_keeps_original_buffers(self):
        from src.palace_scene_update import prepare_palace_patch
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'palace.glb'
            write_fixture(path)
            original = path.read_bytes()
            before, binary_before = read_glb(original)
            patched, stats = prepare_palace_patch(original, lambda x, y: np.zeros(np.broadcast(x, y).shape), self.config)
            after, binary_after = read_glb(patched)
            self.assertEqual(binary_after[:len(binary_before)], binary_before)
            nonpalace = lambda doc: [m for m in doc['meshes'] if not m.get('name', '').startswith('building_way_107918884_0_')]
            self.assertEqual(nonpalace(before), nonpalace(after))
            names = [m['name'] for m in after['meshes']]
            self.assertEqual(len(names), len(set(names)))
            self.assertGreater(stats['exterior_doors'], 8)
            path.write_bytes(patched)
            self.assertTrue(inspect_glb(path)['valid'])


if __name__ == '__main__':
    unittest.main()
