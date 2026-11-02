"""Exact overlay interiors, holes and float32 map coordinates."""
import io
import unittest

import numpy as np
from PIL import Image
from shapely.geometry import Polygon
import trimesh

from src.terrain import Terrain
from src.terrain_surfaces import surface_mesh


class ConformingSurfaceTests(unittest.TestCase):
    def test_float32_collapsed_grid_slivers_are_removed(self):
        terrain = Terrain(np.zeros((3, 3)), (2980, 2980, 3010, 3010))
        polygon = Polygon([(2981, 2994.999999), (3009, 2995.000001),
                           (3008, 2998), (2982, 2998)])
        mesh = surface_mesh(polygon, terrain, 8, .001)
        self.assertTrue(np.all(mesh.area_faces >= 1e-10))
        self.assertAlmostEqual(mesh.area, polygon.area, places=3)

    def test_exported_overlay_follows_every_terrain_plane(self):
        # Unequal horizontal scales, non-integer axes, ridges and saddles.
        terrain = Terrain(np.array([[0., 4., 1., 3.], [3., 0., 6., 2.],
                                    [1., 5., 0., 4.]], dtype=np.float32),
                          (-31.234567, -17.123456, 28.987654, 19.765432))
        polygon = Polygon([(-30, -16), (28, -13), (25, 18), (-29, 17)],
                          holes=[[(-8, -5), (7, -5), (7, 6), (-8, 6)]])
        overlay = surface_mesh(polygon, terrain, 8, .001)
        scene = trimesh.Scene()
        scene.add_geometry(terrain.mesh(Image.new("RGB", (2, 2))), geom_name="terrain")
        scene.add_geometry(overlay, geom_name="overlay")
        loaded = trimesh.load(io.BytesIO(scene.export(file_type="glb")), file_type="glb", force="scene")
        triangles = loaded.geometry["overlay"].triangles
        # Test the full interior, not just vertices or the generator's old probes.
        weights = np.random.default_rng(123).dirichlet([1, 1, 1], 80)
        samples = np.einsum("sk,tkd->tsd", weights, triangles)
        gap = samples[:, :, 2] - terrain(samples[:, :, 0], samples[:, :, 1])
        np.testing.assert_allclose(gap, .001, atol=3e-6, rtol=0)
        horizontal_area = np.abs(np.cross(triangles[:, 1, :2] - triangles[:, 0, :2],
                                          triangles[:, 2, :2] - triangles[:, 0, :2])).sum() / 2
        self.assertAlmostEqual(horizontal_area, polygon.area, places=3)
        self.assertTrue(np.all(overlay.face_normals[:, 2] > 0))


if __name__ == "__main__":
    unittest.main()
