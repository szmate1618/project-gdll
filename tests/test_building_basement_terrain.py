"""Basement terrain cuts retain exterior support, courtyard ground and UVs."""

import io
import unittest

import numpy as np
from PIL import Image
from shapely import union_all
from shapely.affinity import rotate
from shapely.geometry import Polygon, box
import trimesh

from src.building_basement_terrain import carve_basement_terrain
from src.terrain import Terrain


class BasementTerrainTests(unittest.TestCase):
    def test_cuts_overlapping_footprints_and_preserves_the_exterior_surface(self):
        terrain = Terrain(np.array([[0., 2.], [3., 6.]]), (-10, -10, 10, 10))
        scene = trimesh.Scene()
        scene.add_geometry(terrain.mesh(Image.new('RGB', (4, 4), 'green')), geom_name='terrain', node_name='terrain')
        courtyard = Polygon([(-6, -6), (-6, 6), (6, 6), (6, -6)],
                            holes=[[(-2, -2), (-2, 2), (2, 2), (2, -2)]])
        footprints = [courtyard, rotate(box(4, -2, 8, 2), 27)]
        removed = union_all(footprints)
        carve_basement_terrain(scene, footprints)
        # Exercise float32 export and the embedded image/UV path as well.
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
        mesh = restored.geometry['terrain']
        projected = [Polygon(triangle[:, :2]) for triangle in mesh.triangles]
        self.assertAlmostEqual(sum(p.area for p in projected), 400 - removed.area, places=4)
        self.assertLess(sum(p.intersection(removed).area for p in projected), 1e-4)
        np.testing.assert_allclose(mesh.vertices[:, 2], terrain(*mesh.vertices[:, :2].T), atol=1e-6)
        np.testing.assert_allclose(mesh.visual.uv, (mesh.vertices[:, :2] + 10) / 20, atol=1e-6)
        self.assertTrue(np.all(mesh.face_normals[:, 2] > 0))
        self.assertTrue(any(p.covers(box(-.1, -.1, .1, .1).centroid) for p in projected))

    def test_without_basements_keeps_the_original_mesh(self):
        scene = trimesh.Scene()
        scene.add_geometry(Terrain(np.zeros((2, 2)), (-1, -1, 1, 1)).mesh(Image.new('RGB', (2, 2))), geom_name='terrain')
        original = scene.geometry['terrain']
        carve_basement_terrain(scene, [])
        self.assertIs(scene.geometry['terrain'], original)


if __name__ == '__main__':
    unittest.main()
