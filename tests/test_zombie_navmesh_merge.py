import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import shapely


ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/"tools"/(name+".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


baker = module("bake_zombie_navmesh")
merger = module("merge_zombie_navmesh")


class NavmeshMergeTests(unittest.TestCase):
    def write(self, filename, vertices, triangles):
        vertices = np.asarray(vertices, dtype=np.float64)
        triangles = np.asarray(triangles, dtype=np.uint32)
        neighbors = baker.build_neighbors(triangles)
        minimum, maximum = np.min(vertices, axis=0), np.max(vertices, axis=0)
        baker.write_mesh(filename, vertices, triangles, neighbors, (*minimum, *maximum),
                         "12"*32, .31, .003, .06, .007)

    def read_output(self, filename):
        data = filename.read_bytes()
        header = merger.HEADER.unpack_from(data)
        vertices = np.frombuffer(data, "<f8", count=header[1]*2, offset=128).reshape(-1, 2)
        offset = 128+header[11]
        polygons, neighbors = [], []
        for _ in range(header[2]):
            count = struct.unpack_from("<I", data, offset)[0]
            offset += 4
            polygons.append(list(struct.unpack_from("<"+"I"*count, data, offset)))
            offset += 4*count
            neighbors.append(list(struct.unpack_from("<"+"I"*count, data, offset)))
            offset += 4*count
        self.assertEqual(offset, len(data))
        self.assertEqual(header[12], len(data)-128-header[11])
        return data, header, vertices, polygons, neighbors

    def test_square_merges_and_preserves_header_and_exact_vertex_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"source.navmesh", Path(directory)/"merged.navmesh"
            self.write(source, [[0, 0], [1, 0], [1, 1], [0, 1]], [[0, 1, 2], [0, 2, 3]])
            metadata = merger.merge_file(source, output)
            data, header, _, polygons, neighbors = self.read_output(output)
            original = source.read_bytes()
            old_header = merger.HEADER.unpack_from(original)
            self.assertEqual(header[0], b"GDNAV02\0")
            self.assertEqual(header[3:12], old_header[3:12])
            self.assertEqual(header[-1], old_header[-1])
            self.assertEqual(data[128:128+header[11]], original[128:128+header[11]])
            self.assertEqual(len(polygons), 1)
            self.assertEqual(set(polygons[0]), {0, 1, 2, 3})
            self.assertEqual(neighbors[0], [merger.NO_POLYGON]*4)
            self.assertEqual(metadata["triangle_to_polygon"], [0, 0])
            self.assertEqual(metadata["validation"]["geos_covered_triangles"], 2)

    def test_concave_l_cannot_be_merged_across_its_missing_corner(self):
        vertices = [[0, 0], [2, 0], [2, 1], [1, 1], [1, 2], [0, 2]]
        triangles = [[0, 1, 3], [1, 2, 3], [0, 3, 5], [3, 4, 5]]
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"source", Path(directory)/"merged"
            self.write(source, vertices, triangles)
            metadata = merger.merge_file(source, output)
            _, _, points, polygons, _ = self.read_output(output)
            self.assertGreaterEqual(len(polygons), 2)
            actual = shapely.union_all([shapely.Polygon(points[p]) for p in polygons])
            self.assertTrue(shapely.equals(actual, shapely.Polygon(vertices)))
            self.assertFalse(actual.covers(shapely.Point(1.5, 1.5)))
            self.assertEqual(metadata["validation"]["components"], 1)

    def test_vertex_caps_control_hexagon_merging(self):
        angle = np.arange(6)*np.pi/3
        vertices = np.stack((np.cos(angle), np.sin(angle)), axis=1)
        triangles = [[0, i, i+1] for i in range(1, 5)]
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"source", Path(directory)/"merged"
            self.write(source, vertices, triangles)
            counts = []
            for cap in (3, 4, 6):
                metadata = merger.merge_file(source, output, cap)
                _, _, _, polygons, _ = self.read_output(output)
                counts.append(len(polygons))
                self.assertTrue(all(len(p) <= cap for p in polygons))
                self.assertEqual(metadata["validation"]["source_area_m2"],
                                 metadata["validation"]["polygon_area_m2"])
            self.assertEqual(counts[0], 4)
            self.assertEqual(counts[2], 1)
            self.assertGreater(counts[1], 1)
            self.assertLess(counts[1], 4)

    def test_collinear_seam_vertices_remain_and_neighbors_are_reciprocal(self):
        # The left face meets two intervals at a former tile seam. Its midpoint
        # remains in polygon boundaries even when neighboring triangles merge.
        vertices = [[0, 0], [1, 0], [1, .5], [1, 1], [0, 1],
                    [2, 0], [2, .5], [2, 1]]
        triangles = [[0, 1, 2], [0, 2, 3], [0, 3, 4],
                     [1, 5, 6], [1, 6, 2], [2, 6, 7], [2, 7, 3]]
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"source", Path(directory)/"merged"
            self.write(source, vertices, triangles)
            merger.merge_file(source, output)
            _, _, _, polygons, neighbors = self.read_output(output)
            self.assertLess(len(polygons), len(triangles))
            self.assertEqual(set(v for p in polygons for v in p), set(range(len(vertices))))
            for i, polygon in enumerate(polygons):
                for edge, neighbor in zip(merger._edges(polygon), neighbors[i]):
                    if neighbor != merger.NO_POLYGON:
                        reverse = (edge[1], edge[0])
                        other_edge = list(merger._edges(polygons[neighbor])).index(reverse)
                        self.assertEqual(neighbors[neighbor][other_edge], i)

    def test_disconnected_components_and_tiny_double_coordinates_are_preserved(self):
        # At 1 km, these two X coordinates collapse if converted to float32.
        vertices = [[1000, 0], [1000+1e-10, 0], [1000+1e-10, 1], [1000, 1],
                    [1002, 0], [1003, 0], [1002, 1]]
        triangles = [[0, 1, 2], [0, 2, 3], [4, 5, 6]]
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"source", Path(directory)/"merged"
            self.write(source, vertices, triangles)
            metadata = merger.merge_file(source, output)
            _, _, points, polygons, _ = self.read_output(output)
            self.assertEqual(len(polygons), 2)
            self.assertEqual(metadata["validation"]["components"], 2)
            self.assertEqual(points[1, 0], 1000+1e-10)
            self.assertNotEqual(points[0, 0], points[1, 0])

    def test_binary_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            source, first, second = [Path(directory)/name for name in ("source", "first", "second")]
            self.write(source, [[0, 0], [1, 0], [1, 1], [0, 1]], [[0, 1, 2], [0, 2, 3]])
            a = merger.merge_file(source, first)
            b = merger.merge_file(source, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(a["triangle_to_polygon"], b["triangle_to_polygon"])

    def test_malformed_input_preserves_previous_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"source", Path(directory)/"merged"
            self.write(source, [[0, 0], [1, 0], [1, 1], [0, 1]], [[0, 1, 2], [0, 2, 3]])
            merger.merge_file(source, output)
            previous = output.read_bytes()
            sidecar = output.with_name(output.name+".json")
            previous_metadata = sidecar.read_bytes()
            original = bytearray(source.read_bytes())
            # Break one neighbor but retain the valid payload size.
            header = merger.HEADER.unpack_from(original)
            struct.pack_into("<I", original, 128+header[11]+12+8, merger.NO_POLYGON)
            source.write_bytes(original)
            with self.assertRaisesRegex(ValueError, "neighbors"):
                merger.merge_file(source, output)
            source.write_bytes(b"truncated")
            with self.assertRaisesRegex(ValueError, "truncated"):
                merger.merge_file(source, output)
            self.assertEqual(output.read_bytes(), previous)
            self.assertEqual(sidecar.read_bytes(), previous_metadata)
            self.assertFalse(list(Path(directory).glob("*.tmp")))

    def test_independent_coverage_rejects_incorrect_triangle_membership(self):
        vertices = [[0, 0], [1, 0], [0, 1], [2, 0], [3, 0], [2, 1]]
        triangles = [[0, 1, 2], [3, 4, 5]]
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"source", Path(directory)/"merged"
            self.write(source, vertices, triangles)
            merger.merge_file(source, output)
            previous = output.read_bytes()
            wrong = (triangles, np.array([1, 0], dtype=np.uint32), 0)
            with patch.object(merger, "merge_polygons", return_value=wrong):
                with self.assertRaisesRegex(ValueError, "cover an assigned"):
                    merger.merge_file(source, output)
            self.assertEqual(output.read_bytes(), previous)


if __name__ == "__main__":
    unittest.main()
