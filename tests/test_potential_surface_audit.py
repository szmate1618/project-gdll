import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

import numpy as np
import shapely

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from audit_potential_surface import audit, rectangle_test
from prepare_potential_surface_requests import PhysicalGeometry, prepare


class PotentialSurfaceAuditTest(unittest.TestCase):
    def test_rotated_rectangles_against_geos(self):
        random = np.random.default_rng(48291)
        count = 3000
        angles = random.uniform(-np.pi, np.pi, count)
        axes = np.stack((np.cos(angles), np.sin(angles)), axis=1)
        perpendiculars = np.stack((-axes[:, 1], axes[:, 0]), axis=1)
        halves = random.uniform(.05, 2, (count, 2))
        start = random.uniform(-4, 4, (count, 2))
        end = random.uniform(-4, 4, (count, 2))
        actual, distance = rectangle_test(start, end, axes, perpendiculars, halves)
        corners = np.array([(-1,-1),(1,-1),(1,1),(-1,1)])
        points = (axes[:, None, :]*halves[:, None, :1]*corners[None, :, :1]+
                  perpendiculars[:, None, :]*halves[:, None, 1:]*corners[None, :, 1:])
        polygons = shapely.polygons(points)
        segments = shapely.linestrings(np.stack((start, end), axis=1))
        np.testing.assert_array_equal(actual, shapely.intersects(polygons, segments))
        np.testing.assert_allclose(distance, shapely.distance(polygons, segments), atol=2e-14)

    def test_closed_rectangle_contacts(self):
        start = np.array([[-2,1],[-2,1],[-2,1.001],[1,1],[2,1]])
        end = np.array([[2,1],[-1,1],[2,1.001],[1,1],[2,1]])
        axes = np.tile([1,0], (len(start),1))
        perpendiculars = np.tile([0,1], (len(start),1))
        hit, _ = rectangle_test(start, end, axes, perpendiculars, np.ones_like(start))
        np.testing.assert_array_equal(hit, [True,True,False,True,False])

    def run_audit(self, records, *, obstacle=None, final=None, moved_count=None, suffix=b''):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scene = {'obstacles': [] if obstacle is None else [obstacle],
                     'raster': {'minimum':[-5,-5],'maximum':[5,5]}}
            start = [-2,0] if not records else list(records[0][1:3])
            final = final if final is not None else (start if not records else list(records[-1][3:5]))
            summary = {'requests':[{'id':1,'start':start,'goal':final}],
                       'agents':[{'id':1,'final':final}], 'moves_file':'moves.bin',
                       'summary':{'moved_segments':len(records) if moved_count is None else moved_count}}
            (root/'scene.json').write_text(json.dumps(scene))
            (root/'summary.json').write_text(json.dumps(summary))
            with (root/'moves.bin').open('wb') as stream:
                stream.write(b'PSSMOV01')
                for record in records:
                    stream.write(struct.pack('<Idddd',*record))
                stream.write(suffix)
            with contextlib.redirect_stdout(io.StringIO()):
                return audit(root/'scene.json',root/'summary.json',root/'audit.json',chunk_size=1)

    def test_safe_stream_continuity_across_chunks(self):
        result = self.run_audit([(1,-2,0,-1,0),(1,-1,0,0,0),(1,0,0,1,0)])
        self.assertTrue(result['passed'])
        self.assertEqual(result['segments'],3)

    def test_stream_tampering_and_nonfinite_coordinates(self):
        self.assertFalse(self.run_audit([(1,-2,0,-1,0),(1,0,0,1,0)])['passed'])
        self.assertFalse(self.run_audit([(2,-2,0,-1,0)])['passed'])
        self.assertFalse(self.run_audit([(1,-2,0,-1,0)],final=[0,0])['passed'])
        self.assertFalse(self.run_audit([(1,-2,0,-1,0)],moved_count=2)['passed'])
        self.assertFalse(self.run_audit([(1,-2,0,float('nan'),0)],final=[-2,0])['passed'])
        with self.assertRaises(ValueError):
            self.run_audit([(1,-2,0,-1,0)],suffix=b'x')

    def test_circle_tangency_and_rectangle_crossing(self):
        circle = [1,0,0,1,0,.198,.198]
        result = self.run_audit([(1,-1,.5,1,.5)],obstacle=circle)
        self.assertFalse(result['passed'])
        self.assertEqual(result['colliding_segments'],1)
        rectangle = [0,0,0,.6,.8,.5,.5]
        self.assertFalse(self.run_audit([(1,-2,0,2,0)],obstacle=rectangle)['passed'])
        self.assertFalse(self.run_audit([(1,-2,0,4.9,0)])['passed'])

    def test_sampling_uses_original_obb_not_circumscribed_field(self):
        scene = {'obstacles':[[0,0,0,1,0,2,.1]],
                 'raster':{'minimum':[-5,-5],'maximum':[5,5]}}
        geometry = PhysicalGeometry(scene)
        self.assertTrue(geometry.clear([0,1]))
        self.assertFalse(geometry.clear([0,.3]))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'scene.json').write_text(json.dumps(scene))
            with contextlib.redirect_stdout(io.StringIO()):
                a = prepare(root/'scene.json',root/'a.json',20,88291)
                b = prepare(root/'scene.json',root/'b.json',20,88291)
            self.assertEqual(a['requests'],b['requests'])
            for request in a['requests']:
                for name in ('start','goal'):
                    self.assertTrue(geometry.clear(request[name]))
                    np.testing.assert_array_equal(request[name],np.asarray(request[name],dtype=np.float32))


if __name__=='__main__':
    unittest.main()
