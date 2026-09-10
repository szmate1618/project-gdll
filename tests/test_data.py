"""Source boundary checks using small local raster fixtures, without network."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import rasterio
from rasterio.transform import from_origin

from src.data import SourceError, _crop_cog, _dem_tile_name, _valid_osm, _valid_raster, download_sources


class DataTests(unittest.TestCase):
    def setUp(self):
        fixture_root = Path(__file__).resolve().parents[1] / "data" / "raw" / "osm"
        fixture_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="test_sources_", dir=fixture_root)
        self.directory = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_offline_missing_cache_never_requests_network(self):
        config = {"bbox": [19.342, 47.588, 19.369, 47.606], "offline": True}
        with patch("requests.Session.request", side_effect=AssertionError("Network must not run")):
            with self.assertRaisesRegex(SourceError, "Offline mode.*OSM cache"):
                download_sources(config, self.directory)

    def test_crop_preserves_native_pixels_and_georeference(self):
        source_path = self.directory / "synthetic_rgb.tif"
        original = np.arange(3 * 20 * 20, dtype=np.uint16).reshape(3, 20, 20) % 253 + 1
        original = original.astype("uint8")
        transform = from_origin(19.34, 47.61, 0.001, 0.001)
        with rasterio.open(source_path, "w", driver="GTiff", width=20, height=20,
                           count=3, dtype="uint8", crs="EPSG:4326", transform=transform) as source:
            source.write(original)
        destination = self.directory / "cropped.tif"
        bounds = [19.3442, 47.5982, 19.3522, 47.6062]
        _crop_cog(str(source_path), destination, bounds, {"retries": 0}, rgb=True)
        with rasterio.open(destination) as cropped:
            self.assertEqual(cropped.crs.to_epsg(), 4326)
            self.assertAlmostEqual(cropped.res[0], 0.001)
            self.assertAlmostEqual(cropped.res[1], 0.001)
            self.assertLessEqual(cropped.bounds.left, bounds[0])
            self.assertGreaterEqual(cropped.bounds.right, bounds[2])
            self.assertLessEqual(cropped.bounds.bottom, bounds[1])
            self.assertGreaterEqual(cropped.bounds.top, bounds[3])
            np.testing.assert_array_equal(cropped.read(), original[:, 3:12, 4:13])
        self.assertTrue(_valid_raster(destination, rgb=True))
        self.assertFalse(destination.with_suffix(".part.tif").exists())

    def test_corrupt_and_empty_rasters_are_invalid(self):
        corrupt = self.directory / "corrupt.tif"
        corrupt.write_text("interrupted download")
        self.assertFalse(_valid_raster(corrupt))
        empty = self.directory / "empty.tif"
        with rasterio.open(empty, "w", driver="GTiff", width=2, height=2, count=1,
                           dtype="float32", nodata=-9999, crs="EPSG:4326",
                           transform=from_origin(19, 48, 0.001, 0.001)) as target:
            target.write(np.full((1, 2, 2), -9999, dtype="float32"))
        self.assertFalse(_valid_raster(empty))

    def test_partial_overpass_error_is_not_a_usable_cache(self):
        self.assertTrue(_valid_osm({"elements": []}))
        self.assertFalse(_valid_osm({"elements": [], "remark": "runtime error: timeout"}))
        self.assertFalse(_valid_osm({"elements": "truncated"}))

    def test_dem_tile_names_use_signed_southwest_corner(self):
        self.assertEqual(_dem_tile_name(47, 19), "Copernicus_DSM_COG_10_N47_00_E019_00_DEM")
        self.assertEqual(_dem_tile_name(-1, -123), "Copernicus_DSM_COG_10_S01_00_W123_00_DEM")


if __name__ == "__main__":
    unittest.main()
