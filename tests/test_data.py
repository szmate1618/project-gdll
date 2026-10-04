"""Source boundary checks using small local fixtures, without network."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import numpy as np
import rasterio
from rasterio.transform import from_origin

from src.data import (
    CACHE_VERSION, OVERPASS_ENDPOINTS, SourceError, _crop_cog, _dem_tile_name,
    _download_osm, _key, _valid_osm, _valid_raster, download_sources,
)


class DataTests(unittest.TestCase):
    def setUp(self):
        fixture_root = Path(__file__).resolve().parents[1] / "data" / "raw" / "osm"
        fixture_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="test_sources_", dir=fixture_root)
        self.directory = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def osm_session(payload):
        response = Mock()
        response.json.return_value = payload
        response.content = json.dumps(payload).encode("utf-8")
        session = Mock()
        session.request.return_value = response
        return session

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

    def test_osm_query_includes_active_railways_without_operator_filter(self):
        config = {"bbox": [19, 47, 20, 48]}
        session = self.osm_session({"elements": []})
        path, _ = _download_osm(config, self.directory, session)
        query = session.request.call_args.kwargs["data"]["data"]
        self.assertIn('way["railway"~"^(rail|light_rail|narrow_gauge)$"](47,19,48,20);', query)
        self.assertIn('way["building"](47,19,48,20);', query)
        self.assertIn('way["highway"](47,19,48,20);', query)
        self.assertNotIn("operator", query)
        self.assertNotIn("abandoned", query)
        self.assertEqual(path.with_suffix(".overpassql").read_text(encoding="utf-8"), query)

    def test_old_building_and_road_cache_cannot_satisfy_railway_query_offline(self):
        # Reproduce the pre-railway extract's cache identity to exercise migration.
        legacy_query = (
            '[out:json][timeout:90];\n(\n'
            '  way["building"](47,19,48,20);\n'
            '  relation["building"]["type"="multipolygon"](47,19,48,20);\n'
            '  way["highway"](47,19,48,20);\n'
            ');\nout body geom;\n'
        )
        legacy_key = _key({"query": legacy_query, "version": CACHE_VERSION})
        legacy_path = self.directory / f"osm_{legacy_key}.json"
        legacy_path.write_text('{"elements": []}', encoding="utf-8")
        session = Mock()
        config = {"bbox": [19, 47, 20, 48], "offline": True}
        with self.assertRaisesRegex(SourceError, "Offline mode.*OSM cache.*network access"):
            _download_osm(config, self.directory, session)
        session.request.assert_not_called()
        self.assertEqual(json.loads(legacy_path.read_text()), {"elements": []})

    def test_railway_extract_is_reusable_offline(self):
        config = {"bbox": [19, 47, 20, 48]}
        payload = {"elements": [{"type": "way", "id": 1, "tags": {"railway": "rail"}}]}
        path, metadata = _download_osm(config, self.directory, self.osm_session(payload))
        offline_session = Mock()
        cached_path, cached_metadata = _download_osm(
            {**config, "offline": True}, self.directory, offline_session,
        )
        offline_session.request.assert_not_called()
        self.assertEqual(cached_path, path)
        self.assertEqual(cached_metadata, metadata)
        self.assertEqual(json.loads(cached_path.read_text()), payload)

    def test_incomplete_refresh_preserves_last_valid_osm_extract(self):
        config = {"bbox": [19, 47, 20, 48], "network": {"retries": 0}}
        payload = {"elements": [{"type": "way", "id": 1, "tags": {"railway": "light_rail"}}]}
        path, _ = _download_osm(config, self.directory, self.osm_session(payload))
        original = path.read_bytes()
        original_metadata = path.with_suffix(".meta.json").read_bytes()
        incomplete_session = self.osm_session({"elements": [], "remark": "runtime error: timeout"})
        with self.assertRaisesRegex(SourceError, "All Overpass endpoints failed.*runtime error: timeout"):
            _download_osm({**config, "refresh": True}, self.directory, incomplete_session)
        self.assertEqual(incomplete_session.request.call_count, len(OVERPASS_ENDPOINTS))
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(path.with_suffix(".meta.json").read_bytes(), original_metadata)
        self.assertFalse(path.with_suffix(".json.part").exists())
        self.assertIn("runtime error: timeout", path.with_suffix(".http-error.txt").read_text())

    def test_dem_tile_names_use_signed_southwest_corner(self):
        self.assertEqual(_dem_tile_name(47, 19), "Copernicus_DSM_COG_10_N47_00_E019_00_DEM")
        self.assertEqual(_dem_tile_name(-1, -123), "Copernicus_DSM_COG_10_S01_00_W123_00_DEM")


if __name__ == "__main__":
    unittest.main()
