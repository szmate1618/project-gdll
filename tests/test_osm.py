"""Railway extraction checks independent of downloads and mesh generation."""

import unittest

import numpy as np
from pyproj import Transformer
from shapely.geometry import Polygon, box

from src.osm import parse_osm, parse_railways


def way(osm_id, coords, tags):
    return {"type": "way", "id": osm_id, "tags": tags,
            "geometry": [{"lon": x, "lat": y} for x, y in coords]}


class RailwayParsingTests(unittest.TestCase):
    def setUp(self):
        self.transformer = Transformer.from_crs("EPSG:4326", "EPSG:4326", always_xy=True)
        self.clip = box(-10, -10, 10, 10)

    def parse(self, elements, clip=None, origin=(0, 0)):
        return parse_railways({"elements": elements}, self.transformer, origin,
                              self.clip if clip is None else clip)

    def test_track_types_and_missing_operators_without_road_duplication(self):
        tags = [
            {"railway": "rail", "operator": "MÁV"},
            {"railway": "light_rail", "operator": "MÁV-HÉV"},
            {"railway": "rail", "service": "siding"},
            {"railway": "narrow_gauge"},
            {"railway": "platform"}, {"railway": "proposed"},
            {"railway": "abandoned"}, {"railway": "disused"},
            {"railway": "construction", "construction": "rail"},
            {"disused:railway": "rail"}, {"highway": "track"},
        ]
        elements = [way(index, [(-20, index - 5), (20, index - 5)], tag)
                    for index, tag in enumerate(tags)]
        railways = self.parse(elements)
        self.assertEqual([feature["id"] for feature in railways], ["way/0", "way/1", "way/2", "way/3"])
        self.assertEqual([feature["tags"] for feature in railways], tags[:4])
        buildings, roads = parse_osm({"elements": elements}, self.transformer, (0, 0), self.clip)
        self.assertEqual(buildings, [])
        self.assertEqual([feature["id"] for feature in roads], ["way/10"])
        railways[0]["tags"]["operator"] = "edited"
        self.assertEqual(tags[0]["operator"], "MÁV")

    def test_projected_coordinates_relative_to_origin(self):
        transformer = Transformer.from_crs("EPSG:4326", "EPSG:32634", always_xy=True)
        inverse = Transformer.from_crs("EPSG:32634", "EPSG:4326", always_xy=True)
        origin = transformer.transform(19.3555, 47.597)
        coords = [inverse.transform(origin[0] + x, origin[1] + y)
                  for x, y in [(-20, 4), (20, 4)]]
        features = parse_railways({"elements": [way(1, coords, {"railway": "rail"})]},
                                  transformer, origin, self.clip)
        self.assertEqual(len(features), 1)
        np.testing.assert_allclose(features[0]["geometry"].coords, [(-10, 4), (10, 4)], atol=1e-7)

    def test_clip_hole_splits_centerline_with_unique_source_ids(self):
        clip = Polygon(self.clip.exterior.coords,
                       holes=[[(-2, -2), (2, -2), (2, 2), (-2, 2)]])
        features = self.parse([way(7, [(0, 10), (20, 10)], {"railway": "rail"})],
                              clip=clip, origin=(10, 10))
        self.assertEqual([feature["id"] for feature in features], ["way/7:0", "way/7:1"])
        self.assertAlmostEqual(sum(feature["geometry"].length for feature in features), 16)
        for feature in features:
            self.assertTrue(clip.covers(feature["geometry"]))

    def test_referenced_nodes_and_route_members_do_not_duplicate_tracks(self):
        track = {"type": "way", "id": 5, "nodes": [1, 2], "tags": {"railway": "rail"}}
        features = self.parse([
            {"type": "node", "id": 1, "lon": -20, "lat": 0},
            {"type": "node", "id": 2, "lon": 20, "lat": 0},
            track, dict(track),
            {"type": "relation", "id": 10, "tags": {"type": "route", "route": "train"},
             "members": [{"type": "way", "ref": 5, "role": ""}]},
            {"type": "way", "id": 6, "nodes": [1, 999], "tags": {"railway": "rail"}},
            way(7, [(0, 0)], {"railway": "rail"}),
            way(8, [(30, 0), (40, 0)], {"railway": "rail"}),
        ])
        self.assertEqual([feature["id"] for feature in features], ["way/5"])
        np.testing.assert_allclose(features[0]["geometry"].coords, [(-10, 0), (10, 0)])


if __name__ == "__main__":
    unittest.main()
