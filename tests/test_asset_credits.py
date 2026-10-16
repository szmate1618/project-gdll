"""Credits follow the selected published scene, including its acquisition year."""
import unittest

from src.asset_credits import map_credits


class MapCreditsTests(unittest.TestCase):
    def test_selected_scene_and_acquisition_date_take_precedence(self):
        document = {"scene": 1, "scenes": [
            {"extras": {"sources": {"sentinel": {"datetime": "1999-01-01"}}}},
            {"extras": {"sources": {"sentinel": {"acquisition_datetime": "2023-06-02T10:00:00Z"}}}}]}
        notice = map_credits(document, "output/town.glb", "abc123").decode()
        self.assertIn("Contains modified Copernicus Sentinel data 2023", notice)
        self.assertNotIn("1999", notice)
        self.assertIn("SHA-256: abc123", notice)

    def test_unknown_sentinel_date_is_not_silently_replaced(self):
        for date in (None, "unknown date", 2025, "2025"):
            with self.subTest(date=date), self.assertRaisesRegex(ValueError, "acquisition date"):
                map_credits({"scenes": [{"extras": {"sources": {"sentinel": {"datetime": date}}}}]}, "map.glb", "hash")

    def test_sample_is_not_attributed_to_geographic_providers(self):
        notice = map_credits({}, "assets/test.glb", "hash").decode()
        self.assertIn("No geographic source attribution", notice)
        self.assertNotIn("© OpenStreetMap", notice)
        self.assertNotIn("Contains modified Copernicus", notice)

    def test_other_dataset_notices_are_retained(self):
        document = {"scenes": [{"extras": {"sources": {"another_provider": {"attribution": "Credit Example Survey"}}}}]}
        notice = map_credits(document, "map.glb", "hash").decode()
        self.assertIn("Additional dataset: another_provider", notice)
        self.assertIn("Credit Example Survey", notice)


if __name__ == "__main__":
    unittest.main()
