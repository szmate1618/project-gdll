"""Raised doorway landings, side ramps, and conditional front skirts."""

import io
import unittest

import numpy as np
from shapely.geometry import box
import trimesh

from src.building_entrances import Entrance, build_entrances
from src.buildings import add_buildings
from src.terrain import Terrain


class BuildingEntranceTests(unittest.TestCase):
    def entrance(self, height, angle=0):
        direction = np.array([np.cos(angle), np.sin(angle)])
        return Entrance(np.array([1234., -567.]), direction,
                        np.array([direction[1], -direction[0]]), height)

    def test_landing_and_both_ramps_keep_dimensions_and_walkable_normals(self):
        for angle in (0, .63, 2.4):
            entrance = self.entrance(1, angle)
            mesh = build_entrances([entrance], lambda x, y: np.zeros_like(x))
            relative = mesh.vertices[:, :2] - entrance.center
            along = relative @ entrance.direction
            outward = relative @ entrance.outward
            np.testing.assert_allclose([outward.min(), outward.max()], [0, 1.2], atol=1e-4)
            landing = mesh.triangles[np.isclose(mesh.face_normals[:, 2], 1)]
            projected = (landing[:, :, :2] - entrance.center) @ entrance.direction
            np.testing.assert_allclose([projected.min(), projected.max()], [-.75, .75], atol=1e-4)
            np.testing.assert_allclose(landing[:, :, 2], 1.01, atol=1e-6)
            # Both ramps run 2 m along the wall, at the usual interior-stair slope.
            np.testing.assert_allclose([along.min(), along.max()], [-2.75, 2.75], atol=1e-4)
            self.assertTrue(np.all(mesh.face_normals[:, 2] > np.cos(np.pi / 4)))
            np.testing.assert_allclose(mesh.bounds[0, 2], 1.01 - 2 / 1.75, atol=1e-6)
            np.testing.assert_allclose(mesh.area, 1.5 * 1.2 + 2 * 1.2 * np.hypot(2, 2 / 1.75), atol=3e-4)

    def test_front_closes_only_below_40_cm(self):
        for gap in (.20, .399, .40, .401, .8):
            mesh = build_entrances([self.entrance(gap - .01)], lambda x, y: np.zeros_like(x))
            front = mesh.triangles[np.abs(mesh.face_normals[:, 2]) < 1e-5]
            if gap < .4:
                self.assertGreater(len(front), 0)
                np.testing.assert_allclose(front[:, :, 1], -568.2, atol=1e-4)
                self.assertAlmostEqual(front[:, :, 2].min(), -.08, places=6)
            else:
                self.assertEqual(len(front), 0)

    def test_sloping_front_with_a_high_gap_remains_open(self):
        entrance = self.entrance(.3)
        mesh = build_entrances([entrance], lambda x, y: .3 * (x - entrance.center[0]))
        self.assertTrue(np.all(mesh.face_normals[:, 2] > .7))

    def test_front_checks_terrain_triangle_breaks_between_regular_samples(self):
        # A narrow terrain trough falls between the regular half-metre samples.
        # The rendered ground's actual minimum must keep this front open.
        heights = np.zeros((2, 21))
        heights[:, 10] = -.5
        terrain = Terrain(heights, (-1, -2, 1, 0))
        entrance = Entrance(np.zeros(2), np.array([1., 0.]), np.array([0., -1.]), .19)
        mesh = build_entrances([entrance], terrain)
        self.assertTrue(np.all(mesh.face_normals[:, 2] > .7))

    def test_roundtrip_keeps_grey_two_sided_surfaces_outside_wall_lod(self):
        scene = trimesh.Scene()
        add_buildings(scene, [{'id': 'way/1', 'geometry': box(0, 0, 12, 9), 'tags': {'height': '6'}}],
                      lambda x, y: y / 9, {})
        original = scene.geometry['building_way_1_0_entrances']
        rotation = np.array([[1., 0., 0., 0.], [0., 0., 1., 0.], [0., -1., 0., 0.], [0., 0., 0., 1.]])
        scene.apply_transform(rotation)
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
        mesh = restored.geometry['building_way_1_0_entrances']
        self.assertNotIn('building_detail', mesh.metadata)
        self.assertTrue(mesh.visual.material.doubleSided)
        np.testing.assert_array_equal(mesh.visual.material.baseColorFactor, [140, 140, 140, 255])
        self.assertEqual(len(mesh.faces), len(original.faces))
        self.assertTrue(np.all(mesh.area_faces > 1e-10))


if __name__ == '__main__':
    unittest.main()
