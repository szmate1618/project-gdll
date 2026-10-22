import contextlib
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
import shapely

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from bake_potential_surface_guidance import (BUMP_SCALES, bake, frontier,
    make_bumps, make_pockets, structural_bvh, structural_geometry)


def scene(obstacles, maximum=(30, 30)):
    return {'raster': {'minimum': [-30, -30], 'maximum': list(maximum)},
            'obstacles': obstacles}


def u_shape(angle=0):
    cosine, sine = math.cos(angle), math.sin(angle)
    result = []
    for x, z, hx, hz in ((-5, 0, 1, 6), (5, 0, 1, 6), (0, -5, 6, 1)):
        result.append([2, cosine*x-sine*z, sine*x+cosine*z,
                       cosine, sine, hx, hz])
    return result


def pockets(document):
    physical, _, _, blocked, domain = structural_geometry(document)
    records, stats = make_pockets(blocked, domain, physical)
    return records, stats, blocked, domain, physical


class PotentialSurfaceGuidanceTest(unittest.TestCase):
    def test_u_bay_rotates_with_physical_geometry_and_has_safe_exit(self):
        for angle in (0, .37):
            cosine, sine = math.cos(angle), math.sin(angle)
            rows = u_shape(angle)
            # Stagger the wall tops: the cap is a genuine diagonal, avoiding
            # an arithmetic-ulp hull sliver along nominally collinear wall tops.
            rows[1] = [2, cosine*5+sine*.5, sine*5-cosine*.5,
                       cosine, sine, 1, 5.5]
            records, stats, blocked, domain, physical = pockets(scene(rows))
            self.assertEqual(len(records), 1, stats)
            record = records[0]
            polygon = shapely.Polygon(record['outer'], record['holes'])
            mouth = shapely.LineString(record['mouth'])
            expected = [(cosine-10*sine)/math.sqrt(101),
                        (sine+10*cosine)/math.sqrt(101)]
            np.testing.assert_allclose(record['outward'], expected, atol=1e-14)
            self.assertAlmostEqual(record['width'], math.sqrt(101))
            self.assertAlmostEqual(record['depth'], 100/math.sqrt(101))
            self.assertFalse(polygon.intersection(blocked).area)
            self.assertTrue(domain.covers(polygon))
            self.assertTrue(polygon.boundary.difference(mouth).difference(blocked.boundary).is_empty)
            endpoint = np.mean(record['mouth'], axis=0)+2*np.asarray(record['outward'])
            self.assertTrue(physical.clear(endpoint))
            self.assertFalse(blocked.intersects(shapely.LineString((np.mean(record['mouth'], axis=0), endpoint))))
            self.assertNotEqual(record['outer'][0], record['outer'][-1])

    def test_native_slabs_reject_uncertain_rotated_hull_cap(self):
        records, stats, *_ = pockets(scene(u_shape(.37)))
        self.assertEqual(records, [], stats)
        self.assertGreater(stats['pocket_rejections']['mouth_native_slab_contact'], 0)

    def test_closed_courtyard_and_split_opening_are_rejected(self):
        closed = u_shape()+[[2, 0, 5, 1, 0, 6, 1]]
        records, stats, *_ = pockets(scene(closed))
        self.assertEqual(records, [])
        self.assertGreater(stats['pocket_rejections']['closed_or_multiple_mouths'], 0)
        # This separate vertical obstacle crosses the hull opening while leaving
        # the bay connected around its lower tip: two mouths, not one pocket.
        divided = u_shape()+[[2, 0, 6.5, 1, 0, .6, 2]]
        records, stats, *_ = pockets(scene(divided))
        self.assertEqual(records, [], stats)
        self.assertGreater(stats['pocket_rejections']['closed_or_multiple_mouths'], 0)

    def test_map_clipping_cannot_create_a_certified_wall(self):
        records, stats, *_ = pockets(scene(u_shape(), maximum=(0, 30)))
        self.assertEqual(records, [], stats)
        self.assertGreater(stats['pocket_rejections']['artificial_or_domain_seam'], 0)

    def test_holes_preserved_and_trees_only_constrain_exit_endpoint(self):
        island = u_shape()+[[2, 0, 0, 1, 0, .6, .6]]
        records, stats, *_ = pockets(scene(island))
        self.assertEqual(len(records), 1, stats)
        self.assertEqual(len(records[0]['holes']), 1)
        self.assertFalse(shapely.Polygon(records[0]['outer'], records[0]['holes']).contains(shapely.Point(0, 0)))
        ordinary, *_ = pockets(scene(u_shape()+[[1, 0, 0, 1, 0, .4, .4]]))
        self.assertEqual(len(ordinary), 1)
        exit_tree = u_shape()+[[1, 0, 8.302, 1, 0, .4, .4]]
        records, stats, *_ = pockets(scene(exit_tree))
        self.assertEqual(records, [], stats)
        self.assertEqual(stats['pocket_rejections']['physical_exit_endpoint_blocked'], 1)

    def test_bvh_frontiers_are_disjoint_internal_and_deterministic(self):
        rows = [[2, x*18, z*18, 1, 0, 1, 1]
                for x in range(12) for z in range(12)]
        _, _, rectangles, _, _ = structural_geometry(scene(rows, maximum=(250, 250)))
        nodes, order = structural_bvh(rectangles)
        for scale in BUMP_SCALES:
            chosen = frontier(nodes, scale['minimum_span_m'], scale['maximum_span_m'])
            self.assertGreater(len(chosen), 0)
            used = set()
            for index in chosen:
                self.assertNotEqual(index, 0)
                node = nodes[index]
                self.assertGreaterEqual(node.end-node.begin, 2)
                members = set(order[node.begin:node.end])
                self.assertTrue(used.isdisjoint(members))
                used.update(members)
                span = max(node.box[2:]-node.box[:2])
                self.assertGreaterEqual(span, scale['minimum_span_m'])
                self.assertLessEqual(span, scale['maximum_span_m'])
        first, diagnostics = make_bumps(rectangles)
        self.assertEqual(first, make_bumps(rectangles)[0])
        self.assertEqual(set(b['level'] for b in first), {0, 1})
        self.assertEqual(diagnostics['bumps'], len(first))
        for bump in first:
            x0, z0, x1, z1 = bump['box']
            offsets = np.asarray([[x0, z0], [x1, z0], [x1, z1], [x0, z1]])-bump['center']
            self.assertTrue(np.all(np.sum((offsets/bump['radii'])**2, axis=1) < 1))

    def test_empty_scene_and_atomic_provenance_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/'scene.json'
            source.write_text(json.dumps(scene([])))
            with contextlib.redirect_stdout(io.StringIO()):
                result = bake(source, root/'guidance.json')
            self.assertEqual(result['bumps'], [])
            self.assertEqual(result['pockets'], [])
            self.assertEqual(result['schema'], 1)
            self.assertEqual(len(result['source_sha256']), 64)
            self.assertEqual(result, json.loads((root/'guidance.json').read_text()))
            self.assertFalse((root/'guidance.json.tmp').exists())
            with self.assertRaises(ValueError):
                bake(source, root/'guidance.json')


if __name__ == '__main__':
    unittest.main()
