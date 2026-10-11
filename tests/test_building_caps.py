"""Float32 foundation refinement at door corners."""

import unittest

import numpy as np
import trimesh

from src.building_caps import refined_caps
from src.facade_surface import edge_key


class BuildingCapTests(unittest.TestCase):
    def test_rounded_door_corner_across_a_thin_internal_edge(self):
        # A real roof split produces this micrometer-wide cap triangle.
        a, b, c, d, corner = np.array([
            [217.6526641845703, 1688.7479248046875, 0],
            [217.65267944335938, 1688.7479248046875, 0],
            [221.39889526367188, 1685.64990234375, 0],
            [213, 1685, 0],
            [218.0380401611328, 1688.42919921875, 0],
        ])
        caps = [(np.array([a, b, c]), np.array([0, 0, -1])),
                (np.array([a, c, d]), np.array([0, 0, -1]))]
        splits = {edge_key(b, c): {tuple(corner): corner}}
        triangles = np.array([triangle for triangle, _ in refined_caps(caps, splits)])
        self.assertEqual(len(triangles), 3)
        self.assertTrue(np.all(np.cross(triangles[:, 1] - triangles[:, 0],
                                        triangles[:, 2] - triangles[:, 0])[:, 2] < 0))
        mesh = trimesh.Trimesh(triangles.reshape(-1, 3), np.arange(9).reshape(-1, 3))
        self.assertTrue(mesh.is_winding_consistent)
        edges, counts = np.unique(mesh.edges_sorted, axis=0, return_counts=True)
        boundary = {edge_key(*mesh.vertices[edge]) for edge in edges[counts == 1]}
        expected = {edge_key(start, end) for start, end in
                    [(a, b), (b, corner), (corner, c), (c, d), (d, a)]}
        self.assertEqual(boundary, expected)

    def test_unaffected_triangle_keeps_its_geometry_and_normal(self):
        triangle = np.array([[0., 0, 0], [0, 1, 2], [1, 0, 3]])
        normal = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
        normal /= np.linalg.norm(normal)
        refined = list(refined_caps([(triangle, normal)], {}))
        self.assertEqual(len(refined), 1)
        np.testing.assert_array_equal(refined[0][0], triangle)
        np.testing.assert_array_equal(refined[0][1], normal)


if __name__ == '__main__':
    unittest.main()
