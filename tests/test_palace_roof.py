"""Roof massing must preserve the prepared interior and the closed OSM shell."""
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from shapely import distance, points
import trimesh

from palace_fixture import write_fixture, feature
from src.config import load_config
from src.glb import read_glb
from src.palace_roof import hip_height, roof_definition
from src.verify import inspect_glb


class PalaceRoofTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.path = Path(cls.directory.name) / 'palace.glb'
        write_fixture(cls.path)
        cls.scene = trimesh.load_scene(cls.path, process=False)
        cls.config = load_config(Path(__file__).resolve().parents[1] / 'config.json')
        cls.feature = feature(cls.config)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_roof_covers_exact_footprint_and_retains_flat_eaves(self):
        roof = self.scene.geometry['building_way_107918884_0_roof']
        _, index, count = np.unique(roof.edges_sorted, axis=0, return_index=True, return_counts=True)
        edge_vertices = np.unique(roof.edges_sorted[index[count == 1]])
        self.assertGreater(len(edge_vertices), 0)
        self.assertLess(distance(points(roof.vertices[edge_vertices, :2]), self.feature['geometry'].boundary).max(), .001)
        self.assertLess(np.ptp(roof.vertices[edge_vertices, 2]), .00001)
        triangle = roof.triangles[:, :, :2]
        a, b = triangle[:, 1] - triangle[:, 0], triangle[:, 2] - triangle[:, 0]
        area = np.abs(a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0]).sum() / 2
        self.assertAlmostEqual(area, self.feature['geometry'].area, delta=.02)
        self.assertGreater(np.ptp(roof.vertices[:, 2]), 5)

    def test_wings_have_independent_ridges(self):
        # Along both perpendicular wings, roof crests stay above their eaves.
        self.assertGreater(float(hip_height([5.25, 33])), 4)
        self.assertGreater(float(hip_height([-14, 47.9])), 4)
        self.assertGreater(float(hip_height([86.5, 85])), 4)
        self.assertLess(float(hip_height([86.5, 140])), .001)

    def test_roof_furniture_is_closed_and_four_cupolas_are_authored(self):
        self.assertEqual(len(roof_definition()['cupolas']), 4)
        for name, mesh in self.scene.geometry.items():
            if '_roof_' not in name:
                continue
            merged = mesh.copy()
            merged.merge_vertices(merge_tex=True, merge_norm=True)
            self.assertTrue(merged.is_watertight, name)
            self.assertTrue(merged.is_winding_consistent, name)
            self.assertGreater(merged.volume, 0, name)
            self.assertGreater(merged.area_faces.min(), 1e-10, name)
        plaster = self.scene.geometry['building_way_107918884_0_roof_plaster']
        self.assertGreater(plaster.bounds[1, 2], 14)

    def test_interior_is_identical_to_saved_result_and_shell_stays_closed(self):
        doc, _ = read_glb(self.path.read_bytes())
        wall = next(m for m in doc['meshes'] if m['name'] == 'building_way_107918884_0_walls')
        actual = wall['extras']['authored_floorplan']
        saved = json.loads((Path(__file__).resolve().parents[1] /
                           'assets/buildings/godollo-palace/prepared-floorplan.json').read_text())['authored_floorplan']
        # This fixture uses level ground; the saved town has a higher datum.
        saved['origin'][2] = actual['origin'][2]
        self.assertEqual(actual, saved)
        report = inspect_glb(self.path)
        self.assertEqual(report['building_solids_checked'], 1)
        self.assertEqual(report['detailed_building_solids_checked'], 1)


if __name__ == '__main__':
    unittest.main()
