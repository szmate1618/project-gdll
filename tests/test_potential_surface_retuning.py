"""Parameter-selection regressions; these tests never execute a benchmark."""

import itertools
import math
from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import retune_potential_surface as retuning


INHERITED_WEIGHT = 1448.1546878700494


def entry(arrived, remaining, support=12, length=2, weight=INHERITED_WEIGHT):
    return {
        'arrived': arrived,
        'remaining_distance_ratio_clipped_mean': remaining,
        'configuration': {'support': support, 'length_scale': length, 'weight': weight},
    }


class PotentialSurfaceRetuningTest(unittest.TestCase):
    def test_grid_covers_joint_parameters_and_retains_inherited_baseline(self):
        configurations = list(retuning.candidates())
        keys = {retuning.key(configuration) for configuration in configurations}
        self.assertEqual(len(keys), len(configurations))
        for kappa, length, support in itertools.product(
                (16, 64, 256, 1024), (1, 2, 4), (3, 6, 12, 24)):
            expected = {'weight': kappa * length * length,
                        'length_scale': length, 'support': support}
            self.assertIn(retuning.key(expected), keys)
        self.assertTrue(any(configuration['weight'] == INHERITED_WEIGHT and
                            configuration['length_scale'] == 2 and
                            configuration['support'] == 12
                            for configuration in configurations))

    def test_range_ablation_changes_only_support(self):
        configurations = list(retuning.candidates())
        supports = {configuration['support'] for configuration in configurations
                    if configuration['weight'] == INHERITED_WEIGHT and
                    configuration['length_scale'] == 2}
        self.assertTrue({1.5, 3, 6, 12, 24, 48}.issubset(supports))

    def test_weak_filter_controls_use_inherited_field_scales(self):
        configurations = list(retuning.candidates())
        for weight in (0, 4):
            self.assertTrue(any(configuration['weight'] == weight and
                                configuration['length_scale'] == 2 and
                                configuration['support'] == 12
                                for configuration in configurations))

    def test_parameterization_keeps_boundary_strength_when_length_changes(self):
        for length in (.5, 1, 2, 4, 8):
            configuration = retuning.parameters(64, length, 12)
            self.assertEqual(configuration['weight'], 64 * length * length)
            self.assertEqual(configuration['length_scale'], length)
            self.assertEqual(configuration['support'], 12)

    def test_refinement_spans_axes_and_extends_24_meter_support(self):
        center = retuning.parameters(64, 2, 24)
        refined = retuning.refinements([center], [center])
        keys = {retuning.key(configuration) for configuration in refined}
        expected = (
            retuning.parameters(32, 2, 24),
            retuning.parameters(128, 2, 24),
            retuning.parameters(64, math.sqrt(2), 24),
            retuning.parameters(64, 2 * math.sqrt(2), 24),
            retuning.parameters(64, 2, 12 * math.sqrt(2)),
            retuning.parameters(64, 2, 24 * math.sqrt(2)),
            retuning.parameters(64, 2, 48),
        )
        self.assertEqual(keys, {retuning.key(configuration) for configuration in expected})

    def test_refinement_bounds_and_boundary_strength_are_preserved(self):
        centers = [retuning.parameters(.25, .5, 1.5),
                   retuning.parameters(4096, 8, 48)]
        refined = retuning.refinements(centers, centers)
        self.assertEqual(len(refined), 6)
        for configuration in refined:
            self.assertTrue(.25 <= configuration['boundary_strength'] <= 4096)
            self.assertTrue(.5 <= configuration['length_scale'] <= 8)
            self.assertTrue(1.5 <= configuration['support'] <= 48)
            self.assertAlmostEqual(configuration['weight'] / configuration['length_scale'] ** 2,
                                   configuration['boundary_strength'])

    def test_refinement_omits_existing_and_duplicate_candidates_without_mutation(self):
        center = retuning.parameters(64, 2, 24)
        existing = list(retuning.candidates()) + [retuning.parameters(32, 2, 24)]
        before = [dict(configuration) for configuration in existing]
        center_before = dict(center)
        refined = retuning.refinements([center, center], existing)
        keys = [retuning.key(configuration) for configuration in refined]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertFalse(set(keys) & {retuning.key(configuration) for configuration in existing})
        self.assertEqual(existing, before)
        self.assertEqual(center, center_before)

    def test_arrivals_override_remaining_distance(self):
        self.assertLess(retuning.rank(entry(77, 1)), retuning.rank(entry(76, 0)))

    def test_remaining_progress_breaks_arrival_ties_using_clipped_mean(self):
        lower_mean = entry(77, .1)
        lower_mean['remaining_distance_ratio_median'] = .8
        lower_mean['update_cpu_seconds'] = 1000
        higher_mean = entry(77, .2)
        higher_mean['remaining_distance_ratio_median'] = 0
        higher_mean['update_cpu_seconds'] = 0
        self.assertLess(retuning.rank(lower_mean), retuning.rank(higher_mean))

    def test_equal_outcomes_prefer_compact_support_then_deterministic_parameters(self):
        self.assertLess(retuning.rank(entry(77, .1, support=6)),
                        retuning.rank(entry(77, .1, support=12)))
        self.assertLess(retuning.rank(entry(77, .1, length=1)),
                        retuning.rank(entry(77, .1, length=2)))
        self.assertLess(retuning.rank(entry(77, .1, weight=0)),
                        retuning.rank(entry(77, .1, weight=4)))


if __name__ == '__main__':
    unittest.main()
