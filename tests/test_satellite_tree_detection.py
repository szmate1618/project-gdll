"""Forest-colour learning and stable metric pixel-centre spacing."""
from dataclasses import replace
import unittest
import numpy as np
from scipy.spatial import cKDTree
from src.satellite_tree_detection import DetectionSettings,classify_tree_color,learn_forest_color,select_candidates


class SatelliteColorTests(unittest.TestCase):
    def test_forest_mean_rejects_bright_green_and_excludes_perfect_matches(self):
        rgb = np.full((10, 10, 3), [40, 70, 35], dtype=np.uint8)
        training = np.zeros((10, 10), bool)
        training[:5, :5] = True
        rgb[8, 8], rgb[8, 9] = [120, 170, 75], [40, 40, 40]
        model = learn_forest_color(rgb, training)
        np.testing.assert_array_equal(model['mean_rgb'], [40, 70, 35])
        excluded = training.copy()
        all_matches, outside, scores = classify_tree_color(rgb, np.ones((10, 10), bool), excluded, model)
        self.assertTrue(all_matches[0, 0])
        self.assertFalse(outside[0, 0])
        self.assertTrue(outside[7, 7])
        self.assertFalse(outside[8, 8])
        self.assertFalse(outside[8, 9])
        self.assertEqual(scores[7, 7], 1)

    def test_colour_variation_is_learned_without_singular_covariance(self):
        rgb = np.tile(np.arange(30, 80, dtype=np.uint8)[:, None, None], (1, 2, 3))
        rgb[..., 1] += 25
        model = learn_forest_color(rgb, np.ones(rgb.shape[:2], bool))
        self.assertTrue(np.isfinite(model['inverse_covariance_rgb']).all())
        self.assertGreater(model['distance_squared_limit'], 1)
        with self.assertRaisesRegex(ValueError, '25 valid'):
            learn_forest_color(rgb, np.zeros(rgb.shape[:2], bool))
        with self.assertRaisesRegex(ValueError, 'green canopy'):
            learn_forest_color(np.full((5, 5, 3), 80, np.uint8), np.ones((5, 5), bool))

    def test_spacing_uses_metres_and_stable_score_order(self):
        mask = np.ones((3, 10), bool)
        scores = np.ones(mask.shape)
        scores[0, 2] = 2
        coordinates = lambda rows, columns: (columns*4., -rows*20.)
        selected = select_candidates(mask, scores, coordinates, 12.)
        self.assertEqual(selected[0]['column'], 2)
        self.assertEqual(selected, select_candidates(mask, scores, coordinates, 12.))
        points = [(p['east_m'], p['north_m']) for p in selected]
        self.assertEqual(cKDTree(points).query_pairs(12-1e-6), set())
        self.assertEqual({p['row'] for p in selected}, {0, 1, 2})
        self.assertEqual(select_candidates(np.zeros((2, 2), bool), np.ones((2, 2)), coordinates, 12), [])

    def test_invalid_settings_rejected(self):
        for key, value in [('color_quantile', float('nan')), ('color_quantile', .1),
                           ('min_spacing_m', 0), ('training_inset_m', -1)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                replace(DetectionSettings(), **{key: value}).validate()
