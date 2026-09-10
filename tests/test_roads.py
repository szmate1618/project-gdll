"""Geometry and OSM-tag regression checks for the road generator."""

import unittest

import numpy as np
from shapely.geometry import LineString, MultiLineString, Point, Polygon
import trimesh

from src.roads import _surface_mesh, add_roads, parse_width, road_surface, road_width


class RoadTests(unittest.TestCase):
    def test_width_precedence_and_units(self):
        self.assertEqual(road_width({"width": "7.5 m", "lanes": "4", "highway": "primary"}), 7.5)
        self.assertEqual(road_width({"width": "unknown", "lanes": "2", "highway": "footway"}), 6)
        self.assertEqual(road_width({"lanes": "unknown", "highway": "footway"}), 1.8)
        self.assertEqual(road_width({"lanes": "nan", "highway": "service"}), 3.5)
        self.assertAlmostEqual(parse_width("10' 6\""), 3.2004)
        self.assertAlmostEqual(parse_width("12 ft"), 3.6576)
        self.assertIsNone(parse_width("-4"))
        self.assertIsNone(parse_width("0"))

    def test_surfaces_override_highway_defaults(self):
        self.assertEqual(road_surface({"highway": "track", "surface": "asphalt"}), "asphalt")
        self.assertEqual(road_surface({"surface": "compacted"}), "gravel")
        self.assertEqual(road_surface({"surface": "ground"}), "dirt")
        self.assertEqual(road_surface({"surface": "sett"}), "paving_stones")
        self.assertEqual(road_surface({"highway": "track"}), "dirt")

    def test_long_road_follows_slope_without_long_triangles(self):
        scene = trimesh.Scene()
        elevation = lambda x, y: 0.02 * x + 0.08 * y + 3
        stats = add_roads(scene, [{
            "id": "way/10",
            "geometry": LineString([(0, 0), (1000, 0)]),
            "tags": {"highway": "primary", "width": "6"},
        }], elevation, {"roads": {"sample_spacing_m": 10, "offset_m": 0.2}})
        self.assertEqual(stats["road_segments"], 1)
        mesh = scene.geometry["roads_asphalt"]
        self.assertLessEqual(mesh.edges_unique_length.max(), 14.3)
        self.assertLess(len(mesh.faces), 1000)
        np.testing.assert_allclose(mesh.vertices[:, 2], elevation(*mesh.vertices[:, :2].T) + 0.2)
        self.assertTrue(np.all(mesh.face_normals[:, 2] > 0))
        np.testing.assert_allclose(mesh.bounds[:, :2], [[0, -3], [1000, 3]])

    def test_holes_remain_unfilled_and_area_is_preserved(self):
        polygon = Polygon([(0, 0), (40, 0), (40, 40), (0, 40)],
                          holes=[[(12, 12), (28, 12), (28, 28), (12, 28)]])
        mesh = _surface_mesh(polygon, lambda x, y: np.zeros_like(x), 10, 0.2)
        self.assertAlmostEqual(mesh.area, polygon.area, places=6)
        for centroid in mesh.triangles_center:
            self.assertTrue(polygon.covers(Point(centroid[:2])))

    def test_clipping_multilines_and_material_reuse(self):
        scene = trimesh.Scene()
        features = [{
            "id": "way/1",
            "geometry": MultiLineString([[(-20, 10), (120, 10)], [(-20, 30), (120, 30)]]),
            "tags": {"highway": "residential"},
        }, {
            "id": "way/2", "geometry": LineString([(10, 0), (10, 50)]),
            "tags": {"highway": "service", "surface": "asphalt"},
        }, {
            "id": "way/3", "geometry": LineString([(30, 0), (30, 50)]),
            "tags": {"highway": "track", "surface": "gravel"},
        }]
        stats = add_roads(scene, features, lambda x, y: 0, {"_clip_bounds": [0, 0, 100, 50]})
        self.assertEqual(stats["road_segments"], 4)
        self.assertEqual(stats["road_meshes"], 2)
        self.assertEqual(stats["road_materials"], {"asphalt": 3, "gravel": 1})
        for mesh in scene.geometry.values():
            self.assertTrue(np.all(mesh.vertices[:, 0] >= 0))
            self.assertTrue(np.all(mesh.vertices[:, 0] <= 100))
            self.assertTrue(np.all(mesh.vertices[:, 1] >= 0))
            self.assertTrue(np.all(mesh.vertices[:, 1] <= 50))
        # Encoding and reloading exercises reusable PBR materials and topology.
        import io
        loaded = trimesh.load(io.BytesIO(scene.export(file_type="glb")), file_type="glb", force="scene")
        self.assertEqual(len(loaded.geometry), 2)
        self.assertEqual({m.visual.material.name for m in loaded.geometry.values()}, {"road_asphalt", "road_gravel"})

    def test_invalid_spacing_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "sample_spacing_m"):
            add_roads(trimesh.Scene(), [], lambda x, y: 0, {"roads": {"sample_spacing_m": 0}})

    def test_road_interiors_clear_a_terrain_ridge(self):
        from src.terrain import Terrain
        terrain = Terrain(np.array([[0., 3., 0.], [0., 3., 0.], [0., 3., 0.]]), (0, 0, 20, 20))
        footprint = Polygon([(1, 1), (19, 1), (19, 19), (1, 19)])
        mesh = _surface_mesh(footprint, terrain, 8, .25)
        triangles = mesh.triangles
        probes = np.stack((triangles.mean(axis=1), (triangles[:, 0] + triangles[:, 1]) / 2,
                           (triangles[:, 1] + triangles[:, 2]) / 2,
                           (triangles[:, 2] + triangles[:, 0]) / 2))
        gap = probes[:, :, 2] - terrain(probes[:, :, 0], probes[:, :, 1])
        self.assertGreaterEqual(gap.min(), .25 - 1e-10)
        _, inverse = np.unique(mesh.vertices[:, :2].astype(np.float32), axis=0, return_inverse=True)
        for index in range(inverse.max() + 1):
            self.assertLess(np.ptp(mesh.vertices[inverse == index, 2]), 1e-10)


if __name__ == "__main__":
    unittest.main()
