"""Railway path geometry, terrain alignment, and scene export regressions."""

import io
import unittest

import numpy as np
from shapely.geometry import LineString, MultiLineString, Point, Polygon
import trimesh

from src.railways import add_railways
from src.terrain import Terrain


class RailwayTests(unittest.TestCase):
    def test_long_track_follows_terrain_slope_with_bounded_triangles(self):
        scene = trimesh.Scene()
        elevation = lambda x, y: 0.025 * x - 0.04 * y + 7
        stats = add_railways(scene, [{
            "id": "way/10",
            "geometry": LineString([(0, 0), (1000, 0)]),
            "tags": {"railway": "rail"},
        }], elevation, {})
        self.assertEqual(stats["railway_segments"], 1)
        mesh = scene.geometry["railways_ballast"]
        np.testing.assert_allclose(mesh.bounds[:, :2], [[0, -2], [1000, 2]])
        np.testing.assert_allclose(mesh.vertices[:, 2], elevation(*mesh.vertices[:, :2].T) + 0.3)
        self.assertLessEqual(mesh.edges_unique_length.max(), 11.4)
        self.assertLess(len(mesh.faces), 1000)
        self.assertTrue(np.all(mesh.face_normals[:, 2] > 0))

    def test_width_override_does_not_multiply_tracks_or_passenger_lines(self):
        for tags, expected_width in [
            ({"railway": "rail", "tracks": "3", "passenger_lines": "4"}, 4.0),
            ({"railway": "light_rail", "width": "12 ft", "tracks": "2"}, 3.6576),
            ({"railway": "narrow_gauge", "width": "unknown"}, 4.0),
        ]:
            with self.subTest(tags=tags):
                scene = trimesh.Scene()
                add_railways(scene, [{"geometry": LineString([(0, 0), (20, 0)]), "tags": tags}],
                             lambda x, y: 0, {})
                mesh = scene.geometry["railways_ballast"]
                self.assertAlmostEqual(mesh.bounds[1, 1] - mesh.bounds[0, 1], expected_width)

    def test_clipped_multilines_share_ballast_material_after_glb_reload(self):
        scene = trimesh.Scene()
        features = [{
            "id": "way/1",
            "geometry": MultiLineString([[(-20, 10), (120, 10)], [(-20, 30), (120, 30)]]),
            "tags": {"railway": "rail", "operator": "MÁV"},
        }, {
            "id": "way/2",
            "geometry": LineString([(10, -10), (10, 60)]),
            "tags": {"railway": "light_rail", "operator": "MÁV-HÉV"},
        }]
        stats = add_railways(scene, features, lambda x, y: 0, {"_clip_bounds": [0, 0, 100, 50]})
        self.assertEqual(stats["railway_segments"], 3)
        self.assertEqual(stats["railway_meshes"], 1)
        self.assertEqual(stats["railway_categories"], {"rail": 2, "light_rail": 1})
        mesh = scene.geometry["railways_ballast"]
        self.assertTrue(np.all(mesh.vertices[:, :2] >= [0, 0]))
        self.assertTrue(np.all(mesh.vertices[:, :2] <= [100, 50]))
        loaded = trimesh.load(io.BytesIO(scene.export(file_type="glb")), file_type="glb", force="scene")
        self.assertEqual(list(loaded.geometry), ["railways_ballast"])
        reloaded = loaded.geometry["railways_ballast"]
        self.assertEqual(reloaded.visual.material.name, "railway_ballast")
        self.assertEqual(reloaded.metadata["kind"], "railway")
        self.assertEqual(reloaded.metadata["source"], "OpenStreetMap")
        self.assertEqual(set(reloaded.metadata["osm_ids"]), {"way/1", "way/2"})
        self.assertEqual(reloaded.metadata["railway_categories"], {"rail": 2, "light_rail": 1})
        self.assertEqual(len(reloaded.faces), stats["railway_triangles"])

    def test_polygon_clipping_preserves_terrain_boundary_and_holes(self):
        footprint = Polygon([(0, 0), (30, 0), (30, 20), (0, 20)],
                            holes=[[(12, 8), (18, 8), (18, 12), (12, 12)]])
        scene = trimesh.Scene()
        add_railways(scene, [{
            "geometry": LineString([(-10, 10), (40, 10)]),
            "tags": {"railway": "rail", "width": "6"},
        }], lambda x, y: 0, {"_clip_polygon": footprint})
        mesh = scene.geometry["railways_ballast"]
        self.assertAlmostEqual(mesh.area, 30 * 6 - 6 * 4)
        for centroid in mesh.triangles_center:
            self.assertTrue(footprint.covers(Point(centroid[:2])))

    def test_railway_types_and_unusable_features_are_filtered(self):
        scene = trimesh.Scene()
        features = [{
            "id": f"way/{index}", "geometry": LineString([(0, index * 10), (20, index * 10)]),
            "tags": {"railway": category},
        } for index, category in enumerate(["rail", "light_rail", "narrow_gauge", "abandoned", "platform", "tram"])]
        features.extend([
            {"geometry": None, "tags": {"railway": "rail"}},
            {"geometry": LineString([(0, 0), (0.001, 0)]), "tags": {"railway": "rail"}},
            {"geometry": LineString([(200, 200), (220, 200)]), "tags": {"railway": "rail"}},
        ])
        stats = add_railways(scene, features, lambda x, y: 0, {"_clip_bounds": [-5, -5, 30, 60]})
        self.assertEqual(stats["railway_segments"], 3)
        self.assertEqual(stats["railway_categories"], {"rail": 1, "light_rail": 1, "narrow_gauge": 1})
        self.assertEqual(stats["railways_skipped"], 6)

    def test_empty_railway_collection_is_valid(self):
        scene = trimesh.Scene()
        self.assertEqual(add_railways(scene, [], lambda x, y: 0, {}), {
            "railway_segments": 0,
            "railway_meshes": 0,
            "railway_triangles": 0,
            "railway_categories": {},
            "railways_skipped": 0,
        })
        self.assertFalse(scene.geometry)

    def test_invalid_settings_are_rejected_even_with_no_features(self):
        for field, values in {
            "width_m": [0, -1, float("nan"), float("inf")],
            "sample_spacing_m": [0, -1, float("nan"), float("inf")],
            "offset_m": [-1, float("nan"), float("inf")],
        }.items():
            for value in values:
                with self.subTest(field=field, value=value), self.assertRaisesRegex(ValueError, field):
                    add_railways(trimesh.Scene(), [], lambda x, y: 0, {"railways": {field: value}})

    def test_railway_bed_clears_ridge_interiors_and_keeps_seams_closed(self):
        terrain = Terrain(np.array([[0., 3., 0.], [0., 3., 0.], [0., 3., 0.]]), (0, 0, 20, 20))
        scene = trimesh.Scene()
        add_railways(scene, [{
            "geometry": LineString([(1, 10), (19, 10)]),
            "tags": {"railway": "rail", "width": "6"},
        }], terrain, {})
        mesh = scene.geometry["railways_ballast"]
        triangles = mesh.triangles
        probes = np.stack((triangles.mean(axis=1), (triangles[:, 0] + triangles[:, 1]) / 2,
                           (triangles[:, 1] + triangles[:, 2]) / 2,
                           (triangles[:, 2] + triangles[:, 0]) / 2))
        gap = probes[:, :, 2] - terrain(probes[:, :, 0], probes[:, :, 1])
        self.assertGreaterEqual(gap.min(), 0.3 - 1e-10)
        _, inverse = np.unique(mesh.vertices[:, :2].astype(np.float32), axis=0, return_inverse=True)
        for index in range(inverse.max() + 1):
            self.assertLess(np.ptp(mesh.vertices[inverse == index, 2]), 1e-10)

    def test_nonfinite_terrain_elevation_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "non-finite elevations"):
            add_railways(trimesh.Scene(), [{
                "geometry": LineString([(0, 0), (20, 0)]), "tags": {"railway": "rail"},
            }], lambda x, y: float("nan"), {})


if __name__ == "__main__":
    unittest.main()
