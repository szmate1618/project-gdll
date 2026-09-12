"""Exercise offline tree placement with real map grids and synthetic GeoTIFFs."""
import csv
from contextlib import redirect_stderr
import io
import itertools
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import numpy as np
from pyproj import Transformer
import rasterio
from rasterio.transform import Affine

from src.tree_assets import load_impostor
from src.tree_csv import REQUIRED_COLUMNS, merge_detections
from src.tree_placements import prepare_instances
from tests import test_tree_map
from tools.create_tree_impostors import build_impostor
from tools.prepare_tree_instances import main as prepare_main


ASSETS = Path(__file__).resolve().parents[1] / "assets"


def pixel_world(transform, pixel):
    # Work with both Affine 2.x and 3.x without their differing operator APIs.
    x, y = pixel
    return (transform.a * x + transform.b * y + transform.c,
            transform.d * x + transform.e * y + transform.f)


class TreePlacementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = {}
        for species, height in (("tree", 10.), ("pine", 14.)):
            path = ASSETS / f"{species}_impostor.glb"
            cls.assets[species] = path.read_bytes() if path.exists() else build_impostor(species, height, ASSETS)

    def setUp(self):
        # Reuse only the small exported-map fixture, without inheriting or
        # running the map reader's separate test cases a second time.
        self.map = test_tree_map.TreeMapTests()
        self.map.setUp()
        self.addCleanup(self.map.doCleanups)
        self.root = self.map.root
        self.mesh = self.map.write_map()
        (self.root / "assets").mkdir()
        for species, data in self.assets.items():
            (self.root / "assets" / f"{species}_impostor.glb").write_bytes(data)
        root_patch = patch("src.tree_placements.ROOT", self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)
        self.csv = self.root / "detections.csv"
        self.output = self.root / "trees.instances.json"
        ox, oy = self.map.frame["origin"]
        self.transform = Affine(.5, 0, ox - 50, 0, -.25, oy + 25)

    def write_raster(self, name="image.tif", *, transform=None, crs="EPSG:32634"):
        path = self.root / name
        with rasterio.open(path, "w", driver="GTiff", width=200, height=200, count=1,
                           dtype="uint8", transform=self.transform if transform is None else transform,
                           crs=crs) as dataset:
            dataset.write(np.zeros((1, 200, 200), dtype=np.uint8))
        return path

    def row(self, path, *, transform=None, pixel=(120., 80.), dimensions=(40., 20.), score=None):
        transform = self.transform if transform is None else transform
        px, py = pixel
        width, depth = dimensions
        x, y = pixel_world(transform, pixel)
        return {"xmin": px - width / 2, "ymin": py - depth / 2,
                "xmax": px + width / 2, "ymax": py + depth / 2,
                "pixel_x": px, "pixel_y": py, "world_x": x, "world_y": y,
                "image_path": str(path), "score": "" if score is None else score,
                "label": "tree"}

    def write_rows(self, rows):
        with self.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=[*REQUIRED_COLUMNS, "pixel_x", "pixel_y", "score", "label"])
            writer.writeheader()
            writer.writerows(rows)

    def prepare(self, **kwargs):
        result = prepare_instances(self.csv, self.output, config_path=self.root / "config.json",
                                   map_path=self.map.path, **kwargs)
        return result, json.loads(self.output.read_text())

    def exported_ground(self, east, north):
        # Independently intersect the actual float32 exported triangle planes.
        # This catches accidentally using an absolute datum or a cached raster.
        vertices = self.mesh.vertices.astype(np.float32).astype(float)
        for face in self.mesh.faces:
            triangle = vertices[face]
            weights = np.linalg.solve(np.vstack((triangle[:, :2].T, np.ones(3))), [east, north, 1.])
            if np.all(weights >= -1e-9):
                return float(weights @ triangle[:, 2])
        self.fail("Expected test point to be on the published terrain")

    def test_places_trunk_on_exported_ground_with_metric_anisotropic_bbox(self):
        self.write_rows([self.row(self.write_raster())])
        result, document = self.prepare()
        self.assertEqual((result["input"], result["instances"], result["rasters"]), (1, 1, 1))
        instance = document["instances"][0]
        ground = self.exported_ground(10., 5.)
        np.testing.assert_allclose(instance["position"], [10., ground, -5.], rtol=0, atol=1e-9)
        self.assertLess(instance["position"][1], 20.)  # The 211.125 m datum is not added.
        asset = load_impostor(self.root / "assets/tree_impostor.glb")
        np.testing.assert_allclose(instance["dimensions_m"], [20., 10., 5.], rtol=0, atol=1e-9)
        expected_scale = [20. / asset["footprint_m"][0], 1., 5. / asset["footprint_m"][1]]
        np.testing.assert_allclose(instance["scale"], expected_scale, rtol=0, atol=1e-9)
        matrix = np.asarray(instance["matrix"]).reshape(4, 4).T
        np.testing.assert_allclose(matrix @ [0., 0., 0., 1.], [10., ground, -5., 1.])
        np.testing.assert_allclose(matrix[:3, :3], np.diag(expected_scale), atol=1e-9)
        corners = np.array(list(itertools.product(*zip(asset["geometry_bounds_m"]["min"],
                                                       asset["geometry_bounds_m"]["max"]))))
        world_corners = corners @ matrix[:3, :3].T + matrix[:3, 3]
        np.testing.assert_allclose(instance["bounds"]["min"], world_corners.min(axis=0))
        np.testing.assert_allclose(instance["bounds"]["max"], world_corners.max(axis=0))
        self.assertIsNone(instance["source"]["score"])
        self.assertEqual(document["transforms"]["axes"], {"X": "east", "Y": "up", "Z": "south"})

    def test_reprojects_eov_and_preserves_rotated_sheared_pixel_axes(self):
        source_center = Transformer.from_crs("EPSG:4326", "EPSG:23700", always_xy=True).transform(*self.map.frame["center"])
        transform = Affine(.5, .1, source_center[0] - 60., .2, -.4, source_center[1] + 20.)
        raster = self.write_raster(transform=transform, crs="EPSG:23700")
        pixel, width, depth = (125., 80.), 40., 20.
        row = self.row(raster, transform=transform, pixel=pixel, dimensions=(width, depth))
        self.write_rows([row])
        _, document = self.prepare()
        instance = document["instances"][0]
        to_map = Transformer.from_crs("EPSG:23700", "EPSG:32634", always_xy=True)
        world = to_map.transform(row["world_x"], row["world_y"])
        east, north = np.asarray(world) - self.map.frame["origin"]
        ground = self.exported_ground(east, north)
        np.testing.assert_allclose(instance["position"], [east, ground, -north], rtol=0, atol=1e-8)
        asset = document["assets"]["tree"]
        matrix = np.asarray(instance["matrix"]).reshape(4, 4).T
        for axis, half_pixel, base_extent in ((0, (width / 2, 0), asset["footprint_m"][0]),
                                             (2, (0, depth / 2), asset["footprint_m"][1])):
            negative = to_map.transform(*pixel_world(transform, np.asarray(pixel) - half_pixel))
            positive = to_map.transform(*pixel_world(transform, np.asarray(pixel) + half_pixel))
            delta = np.asarray(positive) - negative
            np.testing.assert_allclose(matrix[:3, axis] * base_extent, [delta[0], 0., -delta[1]],
                                       rtol=0, atol=1e-8)
        self.assertGreater(abs(np.dot(matrix[:3, 0], matrix[:3, 2])), .01)
        np.testing.assert_allclose(matrix[:3, 1], [0., 1., 0.], rtol=0, atol=1e-12)
        info = next(iter(document["rasters"].values()))
        self.assertEqual(info["crs"], "EPSG:23700")
        self.assertFalse(info["crs_overridden"])

    def test_filters_geographic_bbox_instead_of_rectangular_terrain_envelope(self):
        xmin, ymin, xmax, ymax = self.map.frame["bounds"]
        to_wgs84 = Transformer.from_crs("EPSG:32634", "EPSG:4326", always_xy=True)
        w, s, e, n = self.map.config["bbox"]
        outside = None
        for point in itertools.product((xmin + .01, xmax - .01), (ymin + .01, ymax - .01)):
            source = np.asarray(point) + self.map.frame["origin"]
            lon, lat = to_wgs84.transform(*source)
            if not (w <= lon <= e and s <= lat <= n):
                outside = source
                break
        self.assertIsNotNone(outside, "Projected envelope should include points outside the geographic bbox")
        transform = Affine(.5, 0, outside[0] - 50, 0, -.25, outside[1] + 25)
        valid = self.row(self.write_raster("inside.tif"))
        invalid = self.row(self.write_raster("outside.tif", transform=transform),
                           transform=transform, pixel=(100., 100.))
        self.write_rows([valid, invalid])
        result, document = self.prepare()
        self.assertEqual(result["input"], 2)
        self.assertEqual(result["outside_area"], 1)
        self.assertEqual(result["instances"], 1)
        self.assertEqual(document["instances"][0]["source"]["row"], 2)

    def test_missing_or_local_crs_requires_an_explicit_override(self):
        local = 'LOCAL_CS["unreferenced",LOCAL_DATUM["unknown",32767],UNIT["metre",1],AXIS["Easting",EAST],AXIS["Northing",NORTH]]'
        for crs in (None, local):
            with self.subTest(crs=crs):
                raster = self.write_raster(crs=crs)
                self.write_rows([self.row(raster)])
                with self.assertRaisesRegex(ValueError, "missing or local-only CRS"):
                    self.prepare()
                result, document = self.prepare(source_crs="EPSG:32634")
                self.assertEqual(result["instances"], 1)
                info = next(iter(document["rasters"].values()))
                self.assertTrue(info["crs_overridden"])
                self.assertEqual(info["crs"], "EPSG:32634")
                self.assertEqual(document["settings"]["source_crs_override"], "EPSG:32634")

    def test_world_pixel_mismatch_preserves_previous_output_and_removes_partial_file(self):
        raster = self.write_raster()
        valid = self.row(raster)
        invalid = self.row(raster)
        invalid["world_x"] += 20.
        self.write_rows([valid, invalid])
        previous = b"previous successfully published instances\n"
        self.output.write_bytes(previous)
        with self.assertRaisesRegex(ValueError, "world_x/world_y disagree"):
            self.prepare()
        self.assertEqual(self.output.read_bytes(), previous)
        self.assertEqual(list(self.root.glob(".trees.instances.json.*.part")), [])

    def test_missing_tiff_does_not_reuse_another_rows_raster(self):
        raster = self.write_raster("available.tif")
        self.write_rows([self.row(raster), self.row("missing_detector_image.tif")])
        with self.assertRaisesRegex(ValueError, "Missing source TIFF.*missing_detector_image"):
            self.prepare(image_roots=[self.root])
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob(".trees.instances.json.*.part")), [])

    def test_source_raster_is_resolved_independently_for_every_image(self):
        origin = pixel_world(self.transform, (40., -20.))
        transform = Affine(self.transform.a, self.transform.b, origin[0],
                           self.transform.d, self.transform.e, origin[1])
        first = self.write_raster("first.tif")
        second = self.write_raster("second.tif", transform=transform)
        self.write_rows([self.row(first), self.row(second, transform=transform)])
        result, document = self.prepare()
        self.assertEqual((result["instances"], result["rasters"]), (2, 2))
        a, b = document["instances"]
        self.assertNotEqual(a["source"]["raster"], b["source"]["raster"])
        np.testing.assert_allclose(np.asarray(b["position"])[[0, 2]] - np.asarray(a["position"])[[0, 2]],
                                   [20., -5.], rtol=0, atol=1e-8)

    def test_confidence_filter_and_explicit_pine_height(self):
        raster = self.write_raster()
        self.write_rows([self.row(raster, score=.3), self.row(raster, score=.8)])
        result, document = self.prepare(species="pine", height_m=12.5, min_score=.5)
        self.assertEqual((result["input"], result["below_min_score"], result["instances"]), (2, 1, 1))
        instance = document["instances"][0]
        self.assertEqual(instance["asset"], "pine")
        self.assertEqual(instance["source"]["score"], .8)
        self.assertEqual(instance["dimensions_m"][1], 12.5)
        self.assertAlmostEqual(instance["scale"][1], 12.5 / 14.)
        self.assertEqual(instance["source"]["row"], 3)

    def test_confidence_filter_rejects_unscored_detections(self):
        self.write_rows([self.row(self.write_raster())])
        with self.assertRaisesRegex(ValueError, "requires a score"):
            self.prepare(min_score=.5)
        self.assertFalse(self.output.exists())

    def test_output_cannot_overwrite_original_csv_referenced_by_merged_input(self):
        self.write_rows([self.row(self.write_raster())])
        original = self.csv
        previous = original.read_bytes()
        merged = self.root / "merged.csv"
        merge_detections([original], merged)
        self.csv, self.output = merged, original
        with self.assertRaisesRegex(ValueError, "must not overwrite an original source CSV"):
            self.prepare()
        self.assertEqual(original.read_bytes(), previous)
        self.assertEqual(list(self.root.glob(".detections.csv.*.part")), [])

    def test_invalid_crs_cli_returns_error_without_publishing_output(self):
        self.write_rows([self.row(self.write_raster())])
        stderr = io.StringIO()
        with redirect_stderr(stderr):
            status = prepare_main([str(self.csv), "--output", str(self.output),
                                   "--config", str(self.root / "config.json"), "--map", str(self.map.path),
                                   "--source-crs", "EPSG:999999"])
        self.assertEqual(status, 1)
        self.assertIn("Tree preparation failed", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
