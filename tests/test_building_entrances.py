"""Raised doorway landings, side ramps, and conditional front skirts."""

import io
import unittest

import numpy as np
from shapely.geometry import box
import trimesh

from src.building_entrances import (Entrance, build_entrances, build_entrance_railings,
                                    classify_entrance)
from src.buildings import add_buildings
from src.terrain import Terrain


class BuildingEntranceTests(unittest.TestCase):
    def landing_front(self, mesh, entrance):
        along = (mesh.triangles[:, :, :2] - entrance.center) @ entrance.direction
        return mesh.triangles[(np.abs(mesh.face_normals[:, 2]) < 1e-5) &
                              (np.max(np.abs(along), axis=1) <= .7501)]

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
            tops = mesh.face_normals[mesh.face_normals[:, 2] > .1]
            self.assertTrue(np.all(tops[:, 2] > np.cos(np.pi / 4)))
            cheeks = mesh.triangles[np.abs(mesh.face_normals[:, 2]) < 1e-5]
            self.assertEqual(len(cheeks), 2)
            np.testing.assert_allclose((cheeks[:, :, :2] - entrance.center) @ entrance.outward, 1.2, atol=1e-4)
            cheek_normals = mesh.face_normals[np.abs(mesh.face_normals[:, 2]) < 1e-5]
            np.testing.assert_allclose(cheek_normals[:, :2] @ entrance.outward, 1, atol=1e-5)
            for triangle in cheeks:
                heights = np.sort(triangle[:, 2])
                np.testing.assert_allclose(heights, [1.01 - 2 / 1.75, 1.01 - 2 / 1.75, 1.01], atol=1e-6)
            np.testing.assert_allclose(mesh.bounds[0, 2], 1.01 - 2 / 1.75, atol=1e-6)
            np.testing.assert_allclose(mesh.area, 1.5 * 1.2 + 2 * 1.2 * np.hypot(2, 2 / 1.75) + 2 * (2 / 1.75), atol=4e-4)

    def test_ramps_start_only_above_the_30_cm_walking_height(self):
        for rise in (0, .15, .299, .30, .301, .31):
            entrance = self.entrance(rise - .01)
            mesh = build_entrances([entrance], lambda x, y: np.zeros_like(x))
            along = (mesh.vertices[:, :2] - entrance.center) @ entrance.direction
            if rise <= .30:
                np.testing.assert_allclose([along.min(), along.max()], [-.75, .75], atol=1e-4)
            else:
                np.testing.assert_allclose([along.min(), along.max()], [-2.75, 2.75], atol=1e-4)

    def test_low_door_with_a_deeper_approach_keeps_its_ramps(self):
        entrance = self.entrance(.24)
        mesh = build_entrances([entrance], lambda x, y: .1 * (y - entrance.center[1]))
        along = (mesh.vertices[:, :2] - entrance.center) @ entrance.direction
        self.assertGreater(along.max(), 2)

    def test_front_closes_only_below_40_cm(self):
        for gap in (.20, .399, .40, .401, .8):
            entrance = self.entrance(gap - .01)
            mesh = build_entrances([entrance], lambda x, y: np.zeros_like(x))
            front = self.landing_front(mesh, entrance)
            if gap < .4:
                self.assertGreater(len(front), 0)
                np.testing.assert_allclose(front[:, :, 1], -568.2, atol=1e-4)
                self.assertAlmostEqual(front[:, :, 2].min(), -.08, places=6)
            else:
                self.assertEqual(len(front), 0)

    def test_sloping_front_with_a_high_gap_remains_open(self):
        entrance = self.entrance(.3)
        mesh = build_entrances([entrance], lambda x, y: .3 * (x - entrance.center[0]))
        self.assertEqual(len(self.landing_front(mesh, entrance)), 0)

    def test_front_checks_terrain_triangle_breaks_between_regular_samples(self):
        # A narrow terrain trough falls between the regular half-metre samples.
        # The rendered ground's actual minimum must keep this front open.
        heights = np.zeros((2, 21))
        heights[:, 10] = -.5
        terrain = Terrain(heights, (-1, -2, 1, 0))
        entrance = Entrance(np.zeros(2), np.array([1., 0.]), np.array([0., -1.]), .19)
        mesh = build_entrances([entrance], terrain)
        self.assertEqual(len(self.landing_front(mesh, entrance)), 0)

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

    def test_crouchable_gap_replaces_ramps_with_three_textured_guard_panels(self):
        terrain = lambda x, y: np.zeros_like(x)
        for height in (1.14, 1.16, 2.8):
            entrance = classify_entrance(self.entrance(height, .63), terrain)
            mesh = build_entrances([entrance], terrain)
            railing = build_entrance_railings([entrance])
            if height < 1.15:
                self.assertIsNone(railing)
                self.assertIsNone(entrance.lower_floor)
                continue
            self.assertEqual(len(mesh.faces), 2)
            self.assertEqual(len(railing.faces), 6)
            np.testing.assert_allclose(mesh.vertices[:, 2], height + .01, atol=1e-5)
            self.assertAlmostEqual(railing.bounds[1, 2] - railing.bounds[0, 2], 1, places=5)
            material = railing.visual.material
            self.assertEqual(material.alphaMode, 'MASK')
            self.assertTrue(material.doubleSided)
            alpha = np.asarray(material.baseColorTexture)[:, :, 3]
            self.assertTrue(np.all(alpha[0] == 255))
            self.assertEqual(alpha[-1, alpha.shape[1] // 2], 0)
            self.assertEqual(alpha[-1, 0], 255)

    def test_high_ground_on_the_under_landing_route_keeps_ramps(self):
        entrance = classify_entrance(self.entrance(1.5), lambda x, y: .5 * (x - 1234 + .5))
        self.assertIsNone(entrance.lower_floor)

    def test_balcony_and_lower_door_survive_glb_export(self):
        scene = trimesh.Scene()
        stats = add_buildings(scene, [{'id': 'way/1', 'geometry': box(0, 0, 12, 9),
                                      'tags': {'height': '6', 'roof:shape': 'flat'}}],
                              lambda x, y: y * .3, {})
        self.assertEqual(stats['doors'], 3)
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
        railing = restored.geometry['building_way_1_0_balcony_railings']
        self.assertEqual(len(railing.faces), 6)
        self.assertNotIn('building_detail', railing.metadata)
        self.assertEqual(railing.visual.material.alphaMode, 'MASK')
        self.assertIsNotNone(railing.visual.material.baseColorTexture)
        self.assertEqual(railing.visual.uv.shape, (12, 2))


if __name__ == '__main__':
    unittest.main()
