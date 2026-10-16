"""Coverage comparisons must preserve holes and reject misaligned datasets."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from shapely.geometry import Polygon, box
from shapely.geometry.polygon import orient

import numpy as np
from PIL import Image
import rasterio
from rasterio.transform import from_origin

from src.forest_comparison import compare_forests, load_forest, load_tree_points, svg_path
from src.forest_comparison_report import create_comparison


def fixture(polygons):
    return {"schema": "godollo.forest_patches", "version": 1,
            "coordinate_frame": {"units": "meters", "coordinate_order": ["east", "north"],
                                 "origin_projected_m": [300000, 5000000], "crs": "EPSG:32634",
                                 "bounds_m": [-100, -100, 100, 100]},
            "source": {"map_sha256": "same-published-map"},
            "patches": [{"id": f"patch-{i}", "polygon": list(orient(p).exterior.coords),
                         "holes": [list(r.coords) for r in orient(p).interiors]}
                        for i, p in enumerate(polygons)]}


class ForestComparisonTests(unittest.TestCase):
    def read(self, document):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "forest.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            return load_forest(path)

    def test_holes_and_area_changes_use_actual_coverage(self):
        # Existing is a 10x10 square with a 2x2 clearing. New shifts 5 m east.
        old = Polygon([(0, 0), (10, 0), (10, 10), (0, 10)],
                      [[(6, 2), (8, 2), (8, 4), (6, 4)]])
        a, ag = self.read(fixture([old]))
        b, bg = self.read(fixture([box(5, 0, 15, 10)]))
        metrics, layers = compare_forests(a, ag, b, bg)
        self.assertEqual(metrics["old_area_m2"], 96)
        self.assertEqual(metrics["new_area_m2"], 100)
        self.assertEqual(metrics["shared_area_m2"], 46)
        self.assertEqual(metrics["added_area_m2"], 54)
        self.assertEqual(metrics["removed_area_m2"], 50)
        self.assertEqual(metrics["net_change_m2"], 4)
        self.assertAlmostEqual(metrics["intersection_over_union"], 46/150)
        self.assertEqual(layers["added"].intersection(box(6, 2, 8, 4)).area, 4)

    def test_duplicate_overlapping_and_open_rings_rejected(self):
        original = fixture([box(0, 0, 10, 10)])
        open_ring = deepcopy(original)
        open_ring["patches"][0]["polygon"].pop()
        with self.assertRaisesRegex(ValueError, "closed"):
            self.read(open_ring)
        with self.assertRaisesRegex(ValueError, "overlap"):
            self.read(fixture([box(0, 0, 10, 10), box(5, 5, 15, 15)]))
        duplicate = fixture([box(0, 0, 10, 10), box(20, 20, 30, 30)])
        duplicate["patches"][1]["id"] = duplicate["patches"][0]["id"]
        with self.assertRaisesRegex(ValueError, "unique"):
            self.read(duplicate)

    def test_different_map_or_origin_is_rejected(self):
        a, geometry = self.read(fixture([box(0, 0, 10, 10)]))
        b = deepcopy(a)
        b["source"]["map_sha256"] = "different-map"
        with self.assertRaisesRegex(ValueError, "fingerprint"):
            compare_forests(a, geometry, b, geometry)
        b = deepcopy(a)
        b["coordinate_frame"]["origin_projected_m"][0] += .01
        with self.assertRaisesRegex(ValueError, "coordinate frames"):
            compare_forests(a, geometry, b, geometry)

    def test_empty_coverage_and_shared_grid_edges(self):
        a, ag = self.read(fixture([]))
        b, bg = self.read(fixture([]))
        metrics, _ = compare_forests(a, ag, b, bg)
        self.assertEqual(metrics["intersection_over_union"], 1)
        self.assertEqual(svg_path(ag, (-100, -100, 100, 100)), "")
        self.assertEqual(svg_path(box(0, 0, 10, 10).difference(box(0, 0, 10, 10)),
                                  (-100, -100, 100, 100)), "")
        _, adjacent = self.read(fixture([box(0, 0, 10, 10), box(10, 0, 20, 10)]))
        self.assertEqual(adjacent.area, 200)

    def test_svg_flips_north_up_and_keeps_holes(self):
        shape = Polygon([(0, 0), (10, 0), (10, 10), (0, 10)],
                        [[(2, 2), (2, 4), (4, 4), (4, 2)]])
        path = svg_path(shape, (0, 0, 10, 10))
        self.assertTrue(path.startswith("M0.000,1000.000"))
        self.assertIn("0.000,0.000", path)
        self.assertEqual(path.count("M"), 2)
        self.assertEqual(path.count("Z"), 2)

    def test_artifacts_align_raster_and_failed_validation_preserves_previous(self):
        scratch_root = Path(__file__).resolve().parents[1] / "build"
        scratch_root.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch_root) as scratch:
            root = Path(scratch)
            old_path, new_path = root / "old.json", root / "new.json"
            document = fixture([box(0, 0, 10, 10)])
            old_path.write_text(json.dumps(document), encoding="utf-8")
            new_path.write_bytes(old_path.read_bytes())
            original = old_path.read_bytes()
            pixels = np.zeros((3, 20, 20), dtype=np.uint8)
            pixels[0, :10] = 255  # North half red; south half blue.
            pixels[2, 10:] = 255
            imagery = root / "satellite.tif"
            with rasterio.open(imagery, "w", driver="GTiff", width=20, height=20,
                               count=3, dtype="uint8", crs="EPSG:32634",
                               transform=from_origin(299900, 5000100, 10, 10)) as target:
                target.write(pixels)
            output = root / "comparison"
            report = create_comparison(old_path, new_path, imagery, output)
            self.assertEqual(report["metrics"]["shared_area_m2"], 100)
            with Image.open(output / "comparison.png") as overview:
                # Uncovered upper/lower corners retain red/blue raster orientation.
                self.assertEqual(overview.getpixel((50, 220)), (255, 0, 0))
                self.assertEqual(overview.getpixel((50, 1000)), (0, 0, 255))
            page = (output / "index.html").read_text(encoding="utf-8")
            self.assertIn("data:image/jpeg;base64,", page)
            self.assertNotIn("@@", page)
            osm_path, points_path = root / "osm.json", root / "osm-trees.json"
            osm_path.write_text(json.dumps(fixture([box(5, 0, 15, 10)])), encoding="utf-8")
            placements = {"schema": "godollo.tree-instances", "version": 1,
                "map": {"map_sha256": "same-published-map", "crs": "EPSG:32634",
                        "units": "meters", "origin_projected_m": [300000, 5000000]},
                "transforms": {"units": "meters", "matrix_authoritative": True,
                    "matrix_layout": "column-major", "axes": {"X": "east", "Y": "up", "Z": "south"}},
                "instances": [{"matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 13, 7, -23, 1],
                               "position": [999, 999, 999]}]}
            points_path.write_text(json.dumps(placements), encoding="utf-8")
            # Matrices override redundant positions; viewer Z is south, map Y is north.
            self.assertEqual(load_tree_points(points_path, document), [[13, 23]])
            report = create_comparison(old_path, new_path, imagery, output,
                                       osm_path=osm_path, osm_trees_path=points_path)
            self.assertEqual(set(report["datasets"]), {"original", "osm", "sentinel-astra"})
            self.assertEqual(report["datasets"]["osm"]["point_count"], 1)
            self.assertEqual(len(report["comparisons"]), 3)
            self.assertEqual(report["comparisons"]["original|osm"]["shared_area_m2"], 50)
            self.assertEqual(report["comparisons"]["osm|sentinel-astra"]["shared_area_m2"], 50)
            page = (output / "index.html").read_text(encoding="utf-8")
            self.assertIn('data-source="osm"', page)
            self.assertIn('cx="565.000" cy="385.000"', page)
            published = {p.name: p.read_bytes() for p in output.iterdir()}
            placements["map"]["map_sha256"] = "stale-osm-points"
            points_path.write_text(json.dumps(placements), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Tree placements do not match"):
                create_comparison(old_path, new_path, imagery, output,
                                   osm_path=osm_path, osm_trees_path=points_path)
            self.assertEqual(published, {p.name: p.read_bytes() for p in output.iterdir()})
            stale_osm = fixture([box(5, 0, 15, 10)])
            stale_osm["source"]["map_sha256"] = "stale-osm-forest"
            osm_path.write_text(json.dumps(stale_osm), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                create_comparison(old_path, new_path, imagery, output, osm_path=osm_path)
            self.assertEqual(published, {p.name: p.read_bytes() for p in output.iterdir()})
            document["source"]["map_sha256"] = "wrong-map"
            new_path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                create_comparison(old_path, new_path, imagery, output)
            self.assertEqual(published, {p.name: p.read_bytes() for p in output.iterdir()})
            self.assertEqual(old_path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
