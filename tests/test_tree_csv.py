"""Detector exports keep their data and identity through repeated merges."""

import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from src.tree_csv import REQUIRED_COLUMNS, iter_detections, merge_detections


ROOT = Path(__file__).resolve().parents[1]


class TreeCSVTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / "data/tmp"
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="tree_csv_", dir=directory)
        self.directory = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def write_csv(self, path, rows, fields=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        fields = list(fields or rows[0].keys())
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        return path

    def row(self, **values):
        return {"xmin": "1", "ymin": "2", "xmax": "11", "ymax": "22",
                "world_x": "376400.5", "world_y": "5272810.25", "image_path": "tile.tif", **values}

    def test_aliases_extra_fields_and_image_resolution(self):
        image = self.directory / "tile.tif"
        image.write_bytes(b"image existence fixture")
        row = self.row(geometry="POLYGON ((1 2, 11 2, 11 22, 1 2))", label="tree, deciduous", score="0.93")
        for old, new in (("xmin", "x_min"), ("ymin", "y_min"), ("xmax", "x_max"), ("ymax", "y_max")):
            row[new] = row.pop(old)
        path = self.write_csv(self.directory / "detections.csv", [row])
        result = list(iter_detections(path))[0]
        self.assertEqual(result["xmin"], "1")
        self.assertNotIn("x_min", result)
        self.assertEqual(result["geometry"], row["geometry"])
        self.assertEqual(result["label"], row["label"])
        self.assertEqual(result["image_path"], str(image))
        self.assertEqual(result["source_csv"], str(path))
        self.assertEqual(result["source_row"], "2")

    def test_union_deterministic_order_and_output_exclusion_on_rerun(self):
        first = self.write_csv(self.directory / "a.csv", [self.row(score="0.9"), self.row(score="0.9")])
        second = self.write_csv(self.directory / "sub/b.csv", [self.row(label="pine", geometry="POINT (1 2)")])
        output = self.directory / "merged.csv"
        result = merge_detections([self.directory, str(self.directory / "**/*.csv"), first], output)
        self.assertEqual(result["rows"], 3)  # Identical detections are retained.
        self.assertEqual(result["inputs"], [first, second])
        rows = list(iter_detections(output))
        self.assertEqual([row["source_csv"] for row in rows], [str(first), str(first), str(second)])
        self.assertEqual([row["source_row"] for row in rows], ["2", "3", "2"])
        self.assertEqual(rows[0]["label"], "")
        self.assertEqual(rows[-1]["score"], "")
        contents = output.read_bytes()
        again = merge_detections([self.directory], output)
        self.assertEqual(again["rows"], 3)
        self.assertEqual(contents, output.read_bytes())

    def test_missing_image_is_preserved_and_merged_provenance_survives(self):
        original = self.write_csv(self.directory / "source/input.csv", [self.row(image_path="unavailable/tile.tif")])
        merged = self.directory / "elsewhere/merged.csv"
        merge_detections([original], merged)
        row = list(iter_detections(merged))[0]
        self.assertEqual(row["image_path"], "unavailable/tile.tif")
        self.assertEqual(row["source_csv"], str(original))
        late_image = original.parent / "unavailable/tile.tif"
        late_image.parent.mkdir()
        late_image.touch()
        self.assertEqual(list(iter_detections(merged))[0]["image_path"], str(late_image))

    def test_invalid_coordinates_and_boxes_fail_with_path_and_row(self):
        for invalid in ({"world_x": "nan"}, {"world_y": "inf"}, {"xmin": "nope"},
                        {"xmax": "1"}, {"ymax": "-1"}, {"pixel_x": "nan"}, {"score": "nan"}):
            with self.subTest(invalid=invalid):
                path = self.write_csv(self.directory / "invalid.csv", [self.row(**invalid)])
                with self.assertRaisesRegex(ValueError, "invalid.csv:2:"):
                    list(iter_detections(path))

    def test_bad_row_does_not_replace_previous_output(self):
        path = self.write_csv(self.directory / "input.csv", [self.row(), self.row(world_y="nan")])
        output = self.directory / "merged.csv"
        output.write_text("existing output must survive")
        with self.assertRaises(ValueError):
            merge_detections([path], output)
        self.assertEqual(output.read_text(), "existing output must survive")
        self.assertEqual(list(self.directory.glob("*.part")), [])

    def test_missing_columns_duplicate_aliases_and_malformed_rows(self):
        path = self.directory / "invalid.csv"
        self.write_csv(path, [{"xmin": "1"}])
        with self.assertRaisesRegex(ValueError, "missing required CSV columns"):
            list(iter_detections(path))
        self.write_csv(path, [self.row(x_min="1")])
        with self.assertRaisesRegex(ValueError, "duplicate columns"):
            list(iter_detections(path))
        path.write_text(",".join(REQUIRED_COLUMNS) + "\n1,2,3\n")
        with self.assertRaisesRegex(ValueError, "expected 7 CSV fields, received 3"):
            list(iter_detections(path))

    def test_multiline_quoted_field_preserves_physical_source_lines(self):
        path = self.write_csv(self.directory / "input.csv", [self.row(note="first\nsecond"), self.row(note="next")])
        rows = list(iter_detections(path))
        self.assertEqual(rows[0]["note"], "first\nsecond")
        self.assertEqual([row["source_row"] for row in rows], ["2", "4"])

    def test_direct_script_runs_from_another_directory(self):
        path = self.write_csv(self.directory / "input.csv", [self.row()])
        output = self.directory / "result.csv"
        result = subprocess.run([sys.executable, str(ROOT / "tools/merge_tree_detections.py"),
                                 str(path), "-o", str(output)], cwd=self.directory,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Merged 1 detections from 1 CSV files", result.stdout)
        self.assertEqual(len(list(iter_detections(output))), 1)

    def test_empty_or_unmatched_inputs_report_useful_error(self):
        output = self.directory / "result.csv"
        with self.assertRaisesRegex(ValueError, "No input files match"):
            merge_detections([str(self.directory / "missing*.csv")], output)
        with self.assertRaisesRegex(ValueError, "No input CSV files remain"):
            merge_detections([self.directory], output)


if __name__ == "__main__":
    unittest.main()
