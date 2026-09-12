"""Validate alpha footprints and padded bounds used for detection placements."""

import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from src.tree_assets import load_impostor
from tools.create_tree_impostors import build_impostor


ASSETS = Path(__file__).resolve().parents[1] / "assets"


def _document(data):
    length = struct.unpack_from("<I", data, 12)[0]
    return json.loads(data[20:20 + length])


def _replace_document(data, document):
    old_length = struct.unpack_from("<I", data, 12)[0]
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    tail = data[20 + old_length:]
    return (struct.pack("<4sIII4s", b"glTF", 2, 20 + len(encoded) + len(tail), len(encoded), b"JSON")
            + encoded + tail)


class TreeAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.models = {}
        for species, height in (("tree", 10.0), ("pine", 14.0)):
            asset = ASSETS / f"{species}_impostor.glb"
            cls.models[species] = asset.read_bytes() if asset.exists() else build_impostor(species, height, ASSETS)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "impostor.glb"

    def _load(self, data):
        self.path.write_bytes(data)
        return load_impostor(self.path)

    def test_existing_models_return_union_calibration_and_full_geometry(self):
        for species, data in self.models.items():
            with self.subTest(species=species):
                result = self._load(data)
                document = _document(data)
                sources = document["extras"]["sourceTextures"]
                height = document["extras"]["visibleHeightMeters"]
                coordinates = [[], [], []]
                for index, source in enumerate(sources):
                    left, top, right, bottom = source["visibleBoundsPixels"]
                    px, py = source["pivotPixels"]
                    scale = source["metersPerPixel"]
                    for image_x in (left, right):
                        for image_y in (top, bottom):
                            if index == 0:
                                point = ((image_x - px) * scale, (py - image_y) * scale, 0)
                            elif index == 1:
                                point = (0, (py - image_y) * scale, (px - image_x) * scale)
                            else:
                                point = ((image_x - px) * scale, height * (0.5 if index == 2 else 2 / 3),
                                         (image_y - py) * scale)
                            for axis, value in enumerate(point):
                                coordinates[axis].append(value)
                expected = {"min": [min(axis) for axis in coordinates],
                            "max": [max(axis) for axis in coordinates]}
                self.assertEqual(result["visible_bounds_m"], expected)
                self.assertEqual(result["footprint_m"], [max(coordinates[axis]) - min(coordinates[axis]) for axis in (0, 2)])
                self.assertEqual(result["visible_height_m"], height)
                positions = [document["accessors"][primitive["attributes"]["POSITION"]]
                             for primitive in document["meshes"][0]["primitives"]]
                self.assertEqual(result["geometry_bounds_m"], {
                    "min": [min(accessor["min"][axis] for accessor in positions) for axis in range(3)],
                    "max": [max(accessor["max"][axis] for accessor in positions) for axis in range(3)],
                })
                self.assertLess(result["geometry_bounds_m"]["min"][1], 0)
                self.assertGreater(result["geometry_bounds_m"]["max"][1], height)
                self.assertEqual(result["path"], str(self.path.resolve()))
                self.assertEqual(result["sha256"], hashlib.sha256(data).hexdigest())
                self.assertEqual(json.loads(json.dumps(result, allow_nan=False)), result)

    def test_horizontal_cards_contribute_to_both_footprint_axes(self):
        data = self.models["tree"]
        document = _document(data)
        sources = document["extras"]["sourceTextures"]
        for source in sources[:2]:
            pivot_x = source["pivotPixels"][0]
            source["visibleBoundsPixels"][0] = pivot_x - 1
            source["visibleBoundsPixels"][2] = pivot_x + 1
        width, depth = sources[2]["canvasPixels"]
        sources[2]["visibleBoundsPixels"] = [0, 0, width, depth]
        result = self._load(_replace_document(data, document))
        scale = sources[0]["metersPerPixel"]
        self.assertAlmostEqual(result["footprint_m"][0], width * scale)
        self.assertAlmostEqual(result["footprint_m"][1], depth * scale)
        self.assertEqual(result["visible_bounds_m"]["min"][1], 0)

    def test_bad_calibration_is_rejected(self):
        data = self.models["tree"]
        original = _document(data)

        def change_height(document):
            document["extras"]["visibleHeightMeters"] = 0

        def change_scale(document):
            document["extras"]["sourceTextures"][1]["metersPerPixel"] = float("nan")

        def change_bounds(document):
            document["extras"]["sourceTextures"][2]["visibleBoundsPixels"][2] = 2000

        def change_pivot(document):
            document["extras"]["sourceTextures"][0]["pivotPixels"][1] -= 10

        def change_geometry(document):
            accessor = document["meshes"][0]["primitives"][0]["attributes"]["POSITION"]
            document["accessors"][accessor]["min"][0] -= 1

        def change_transform(document):
            document["nodes"][0]["translation"] = [10, 0, 0]

        def change_metadata(document):
            del document["extras"]["sourceTextures"]

        def change_slice(document):
            document["extras"]["planes"][3]["heightMeters"] += 1

        def change_units(document):
            document["extras"]["units"] = "centimeters"

        for mutate in (change_height, change_scale, change_bounds, change_pivot, change_geometry,
                       change_transform, change_metadata, change_slice, change_units):
            with self.subTest(case=mutate.__name__):
                document = copy.deepcopy(original)
                mutate(document)
                with self.assertRaisesRegex(ValueError, "Cannot read tree impostor"):
                    self._load(_replace_document(data, document))

    def test_truncated_or_invalid_glb_is_rejected(self):
        data = self.models["pine"]
        for invalid in (b"", b"glTF", data[:-1], b"nope" + data[4:]):
            with self.subTest(length=len(invalid)), self.assertRaisesRegex(ValueError, "Cannot read tree impostor"):
                self._load(invalid)
        with self.assertRaisesRegex(ValueError, "Cannot read tree impostor"):
            load_impostor(Path(self.temp.name) / "missing.glb")

    def test_hash_includes_binary_payload(self):
        data = self.models["tree"]
        first = self._load(data)
        modified = data[:-1] + bytes([data[-1] ^ 1])
        second = self._load(modified)
        self.assertNotEqual(first["sha256"], second["sha256"])
        self.assertEqual(first["footprint_m"], second["footprint_m"])


if __name__ == "__main__":
    unittest.main()
