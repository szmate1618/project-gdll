"""Satellite trace subdivision keeps mapped gaps and rejects ambiguous input."""
import copy
import unittest

from shapely.geometry import Point, Polygon
from shapely.ops import unary_union

from src.forest_review import build_patches


def fixture():
    return {
        "schema": "godollo.astra_satellite_forest_traces", "version": 1,
        "coordinate_frame": {"bounds_m": [-1200, -1200, 1200, 1200]},
        "parameters": {"grid_cell_size_m": 500, "coordinate_precision_m": .001,
                       "terrain_edge_inset_m": 2, "canopy_edge_inset_m": 0,
                       "clearing_margin_m": 0, "track_half_width_m": 5,
                       "minimum_component_area_m2": 0, "minimum_patch_area_m2": 0},
        "regions": [{"region_id": "woods", "label": "Woods", "kind": "forest",
                     "confidence": "medium", "polygon": [[-600, -600], [600, -600],
                     [600, 600], [-600, 600], [-600, -600]],
                     "excluded_clearings": [[[100, 100], [200, 100], [200, 200],
                                              [100, 200], [100, 100]]],
                     "excluded_track_centrelines": [[[-600, 50], [600, 50]]]}],
    }


class ForestReviewTests(unittest.TestCase):
    def test_grid_preserves_clearing_and_track(self):
        regions, records = build_patches(fixture())
        polygons = [Polygon(p["polygon"], p["holes"]) for p in records]
        woodland = unary_union(polygons)
        self.assertAlmostEqual(woodland.area, 1440000 - 10000 - 12000)
        self.assertAlmostEqual(sum(p.area for p in polygons), woodland.area)
        self.assertFalse(woodland.covers(Point(150, 150)))
        self.assertFalse(woodland.covers(Point(-450, 50)))
        self.assertTrue(woodland.covers(Point(-450, 100)))
        self.assertEqual(regions[0]["patch_count"], len(records))
        for record, polygon in zip(records, polygons):
            self.assertTrue(polygon.is_valid)
            self.assertTrue(polygon.exterior.is_ccw)
            self.assertTrue(all(not h.is_ccw for h in polygon.interiors))
            self.assertLessEqual(polygon.bounds[2] - polygon.bounds[0], 500)
            self.assertLessEqual(polygon.bounds[3] - polygon.bounds[1], 500)

    def test_rejects_invalid_or_overlapping_regions(self):
        trace = fixture()
        trace["regions"][0]["polygon"] = [[0, 0], [100, 100], [0, 100], [100, 0], [0, 0]]
        with self.assertRaisesRegex(ValueError, "Invalid forest review polygon"):
            build_patches(trace)
        trace = fixture()
        second = copy.deepcopy(trace["regions"][0])
        second["region_id"] = "overlap"
        trace["regions"].append(second)
        with self.assertRaisesRegex(ValueError, "overlap"):
            build_patches(trace)

    def test_rounding_and_subdivision_are_deterministic(self):
        trace = fixture()
        before = copy.deepcopy(trace)
        self.assertEqual(build_patches(trace), build_patches(trace))
        self.assertEqual(trace, before)


if __name__ == "__main__":
    unittest.main()
