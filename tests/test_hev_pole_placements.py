"""HÉV midpoint rows, continuous way seams, curves and excluded rail branches."""
import math
import unittest

from shapely.geometry import LineString

from src.hev_pole_placements import make_hev_columns


def track(coords, **tags):
    return {"geometry": LineString(coords),
            "tags": {"railway": "light_rail", "operator": "MÁV-HÉV", "usage": "main", **tags}}


class HevPolePlacementTests(unittest.TestCase):
    def test_one_centered_row_with_spacing_across_reversed_way_seams(self):
        features = [track([(0, 10), (80, 10)]), track([(200, 10), (80, 10)]),
                    track([(200, 14), (0, 14)]),
                    track([(0, 12), (200, 12)], railway="rail", operator="MÁV"),
                    track([(0, 11), (200, 11)], service="yard"),
                    track([(100, 10), (100, 14)], service="crossover")]
        columns = make_hev_columns(features, lambda x, north: x * .1 + north)
        self.assertEqual(len(columns), 4)
        for column, x in zip(columns, (25, 75, 125, 175)):
            self.assertEqual(column["position"], [x, x * .1 + 12, -12])
            self.assertEqual(column["height_m"], 8)
            self.assertEqual(column["width_m"], .3)
        self.assertEqual(columns, make_hev_columns(features[::-1], lambda x, north: x * .1 + north))

    def test_curved_tracks_have_centered_poles_spaced_along_the_route(self):
        features = [track([(radius * math.cos(i / 200), radius * math.sin(i / 200))
                           for i in range(201)]) for radius in (300, 304)]
        columns = make_hev_columns(features, lambda *_: 17)
        self.assertEqual(len(columns), 6)
        angles = []
        for column in columns:
            x, height, z = column["position"]
            self.assertAlmostEqual(math.hypot(x, z), 302, delta=.03)
            self.assertEqual(height, 17)
            angles.append(math.atan2(-z, x))
        for a, b in zip(angles, angles[1:]):
            self.assertAlmostEqual(abs(a - b) * 302, 50, delta=.1)

    def test_single_tracks_and_divergent_or_distant_tracks_have_no_row(self):
        first = track([(0, 0), (200, 0)])
        for second in (None, track([(0, 20), (200, 20)]),
                       track([(100, -50), (100, 50)]),
                       track([(0, 4), (200, 4)], electrified="no")):
            self.assertEqual(make_hev_columns([first] + ([second] if second else []), lambda *_: 0), [])

    def test_pairing_stops_at_other_track_endpoint(self):
        columns = make_hev_columns([track([(0, 0), (200, 0)]),
                                    track([(50, 4), (150, 4)])], lambda *_: 0)
        self.assertEqual(len(columns), 2)
        for column in columns:
            self.assertGreaterEqual(column["position"][0], 50)
            self.assertLessEqual(column["position"][0], 150)
            self.assertEqual(column["position"][2], -2)


if __name__ == "__main__":
    unittest.main()
