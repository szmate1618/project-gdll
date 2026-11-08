"""Basement entrances must leave the published terrain intact."""

import io
import unittest

import numpy as np
from PIL import Image
from shapely.geometry import box
import trimesh

from src.buildings import add_buildings
from src.terrain import Terrain


class BasementTerrainTests(unittest.TestCase):
    def test_balconies_and_basements_preserve_the_complete_terrain(self):
        heights = np.repeat(np.linspace(2.6, 0, 3)[:, None], 3, axis=1)
        terrain = Terrain(heights, (0, 0, 12, 9))
        mesh = terrain.mesh(Image.new('RGB', (4, 4), 'green'))
        scene = trimesh.Scene()
        scene.add_geometry(mesh, geom_name='terrain', node_name='terrain')
        before = (mesh.vertices.copy(), mesh.faces.copy(), mesh.visual.uv.copy())
        stats = add_buildings(scene, [{'id': 'way/1', 'geometry': box(0, 0, 12, 9),
                                      'tags': {'height': '6', 'roof:shape': 'flat'}}], terrain, {})
        self.assertEqual(stats['doors'], 3)
        self.assertIn('building_way_1_0_balcony_railings', scene.geometry)
        self.assertIs(scene.geometry['terrain'], mesh)
        for original, after in zip(before, (mesh.vertices, mesh.faces, mesh.visual.uv)):
            np.testing.assert_array_equal(original, after)
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
        np.testing.assert_array_equal(restored.geometry['terrain'].faces, before[1])
        np.testing.assert_allclose(restored.geometry['terrain'].vertices, before[0], atol=1e-6)


if __name__ == '__main__':
    unittest.main()
