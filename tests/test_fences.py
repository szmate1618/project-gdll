"""Fence geometry, terrain contacts and physical entrance regressions."""
import io
import unittest

import numpy as np
from shapely.geometry import LineString, Point, box
import trimesh

from src.fences import add_fences, fence_style
from src.terrain import Terrain


def feature(coords, tags=None, openings=()):
    return {"id": "way/10", "geometry": LineString(coords),
            "tags": {"barrier": "fence", **(tags or {})}, "openings": openings}


def opening(x, y, width="2", identifier="node/1"):
    return {"id": identifier, "geometry": Point(x, y),
            "tags": {"barrier": "gate", "width": width}}


class FenceTests(unittest.TestCase):
    def test_height_units_materials_and_batched_export(self):
        scene = trimesh.Scene()
        features = [feature([(0, 0), (10, 0)], {"height": "6 ft", "fence_type": "chain_link"}),
                    feature([(0, 5), (10, 5)]),
                    feature([(0, 10), (10, 10)], {"material": "wood"}),
                    feature([(0, 15), (10, 15)], {"fence_type": "railing"})]
        stats = add_fences(scene, features, lambda x, y: 4, {})
        self.assertEqual(stats["fence_ways"], 4)
        self.assertEqual(stats["fence_meshes"], 3)
        self.assertEqual(stats["fence_materials"], {"chain_link": 2, "wood": 1, "metal": 1})
        self.assertAlmostEqual(scene.geometry["fences_chain_link"].bounds[1, 2], 4 + 1.8288)
        loaded = trimesh.load_scene(io.BytesIO(scene.export(file_type="glb")), file_type="glb", process=False)
        self.assertEqual(len(loaded.geometry), 3)
        self.assertEqual(loaded.geometry["fences_chain_link"].visual.material.alphaMode, "MASK")
        texture = np.array(loaded.geometry["fences_chain_link"].visual.material.baseColorTexture)
        self.assertTrue(np.any(texture[:, :, 3] == 0))
        self.assertTrue(np.any(texture[:, :, 3] == 255))
        self.assertEqual(loaded.geometry["fences_wood"].visual.material.alphaMode, "OPAQUE")
        for mesh in loaded.geometry.values():
            self.assertTrue(np.isfinite(mesh.vertices).all())
            self.assertTrue(np.all(mesh.area_faces > 1e-10))
        self.assertEqual(fence_style({"material": "concrete"}), "concrete")

    def test_posts_are_textured_without_extra_geometry(self):
        for style, tags in [("chain_link", {}), ("metal", {"fence_type": "railing"}),
                            ("wood", {"material": "wood"}),
                            ("concrete", {"material": "concrete"})]:
            with self.subTest(style=style):
                scene = trimesh.Scene()
                stats = add_fences(scene, [feature([(0, 0), (10, 0)], tags)],
                                   lambda x, y: 0, {"fences": {"post_spacing_m": 2}})
                self.assertEqual(list(scene.geometry), [f"fences_{style}"])
                self.assertEqual(stats["fence_triangles"], 10)
                mesh = scene.geometry[f"fences_{style}"]
                np.testing.assert_array_equal(mesh.vertices[:, 1], 0)
                np.testing.assert_allclose(mesh.visual.uv[::2, 0], [0, 1, 2, 3, 4, 5])
                texture = np.array(mesh.visual.material.baseColorTexture)
                # The post spans the entire texture height and wraps at U=0.
                self.assertTrue(np.all(texture[:, [0, -1], 3] == 255))
                self.assertTrue(np.all(texture[:, 0, 0] < 255))
                if style in {"chain_link", "metal"}:
                    # The space between posts retains open wire/bar texels.
                    self.assertTrue(np.any(texture[:, texture.shape[1] // 8, 3] == 0))
                else:
                    self.assertTrue(np.all(texture[:, :, 3] == 255))

    def test_panels_follow_actual_terrain_triangles_across_ridges_and_corners(self):
        terrain = Terrain(np.array([[0., 4., 0.], [1., -2., 3.], [0., 5., 0.]]),
                          (0, 0, 20, 20), datum=240)
        scene = trimesh.Scene()
        add_fences(scene, [feature([(1, 4), (18, 17), (8, 19)])], terrain,
                   {"fences": {"post_spacing_m": 7, "default_height_m": 2}})
        mesh = scene.geometry["fences_chain_link"]
        lower, upper = mesh.vertices[::2], mesh.vertices[1::2]
        np.testing.assert_allclose(lower[:, 2], terrain(*lower[:, :2].T) - .05, atol=1e-10)
        np.testing.assert_allclose(upper[:, 2] - lower[:, 2], 2.05, atol=1e-10)
        # Test intermediate contacts, rather than just sampled vertex formulas:
        # each lower panel edge must remain .05 m below the rendered terrain.
        for fraction in (.25, .5, .75):
            probes = lower[:-1] * (1 - fraction) + lower[1:] * fraction
            np.testing.assert_allclose(probes[:, 2], terrain(*probes[:, :2].T) - .05, atol=1e-9)
        self.assertTrue(np.any(np.all(np.isclose(lower[:, :2], [18, 17]), axis=1)))

    def test_gate_gap_is_clear_in_panels(self):
        scene = trimesh.Scene()
        stats = add_fences(scene, [feature([(-10, 0), (10, 0)], openings=[opening(0, 0, "4")])],
                           lambda x, y: 0, {})
        self.assertEqual(stats["fence_segments"], 2)
        self.assertEqual(stats["fence_openings"], 1)
        self.assertAlmostEqual(stats["fence_length_m"], 16)
        for mesh in scene.geometry.values():
            self.assertTrue(np.all(np.abs(mesh.vertices[:, 0]) >= 2 - 1e-9))
            self.assertTrue(np.all(np.abs(mesh.triangles_center[:, 0]) >= 2 - 1e-9))

    def test_gate_at_closed_way_seam_cuts_both_sides(self):
        scene = trimesh.Scene()
        stats = add_fences(scene, [feature([(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)],
                                         openings=[opening(0, 0)])], lambda x, y: 0, {})
        self.assertAlmostEqual(stats["fence_length_m"], 38)
        mesh = scene.geometry["fences_chain_link"]
        self.assertFalse(np.any(np.all(np.isclose(mesh.vertices[:, :2], [0, 0]), axis=1)))

    def test_diagonal_gate_preserves_the_clear_gap(self):
        scene = trimesh.Scene()
        add_fences(scene, [feature([(-10, -10), (10, 10)], openings=[opening(0, 0, "4")])],
                   lambda x, y: 0, {})
        panel = scene.geometry["fences_chain_link"]
        along = panel.vertices[:, :2].sum(axis=1) / np.sqrt(2)
        self.assertTrue(np.all(np.abs(along) >= 2 - 1e-9))

    def test_clip_boundary_gate_and_overlapping_openings(self):
        scene = trimesh.Scene()
        stats = add_fences(scene, [feature([(-10, 0), (10, 0)], openings=[opening(-.5, 0, "3"),
                                            opening(0, 0, "2", "node/2")])],
                           lambda x, y: 0, {"_clip_polygon": box(0, -5, 8, 5)})
        self.assertAlmostEqual(stats["fence_length_m"], 7)
        panel = scene.geometry["fences_chain_link"]
        np.testing.assert_allclose(panel.bounds[:, 0], [1, 8])

    def test_no_fences_invalid_features_and_configuration(self):
        scene = trimesh.Scene()
        self.assertEqual(add_fences(scene, [], lambda x, y: 0, {})["fence_meshes"], 0)
        self.assertFalse(scene.geometry)
        invalid = [feature([(0, 0), (0, 0)]), feature([(0, 0), (10, 0)], {"barrier": "wall"})]
        stats = add_fences(scene, invalid, lambda x, y: 0, {})
        self.assertEqual(stats["fences_skipped"], 2)
        with self.assertRaisesRegex(ValueError, "post_spacing_m"):
            add_fences(scene, [], lambda x, y: 0, {"fences": {"post_spacing_m": 0}})
        with self.assertRaisesRegex(ValueError, "non-finite"):
            add_fences(scene, [feature([(0, 0), (10, 0)])], lambda x, y: np.nan, {})


if __name__ == "__main__":
    unittest.main()
