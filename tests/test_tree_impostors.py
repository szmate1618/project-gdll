"""Check the reusable impostors' texture embedding, UVs and plane placement."""

import io
import json
from pathlib import Path
import struct
import unittest

import numpy as np
import trimesh

from tools.create_tree_impostors import build_impostor


ASSETS = Path(__file__).resolve().parents[1] / "assets"


def read_glb(data):
    magic, version, length = struct.unpack_from("<4sII", data)
    if (magic, version, length) != (b"glTF", 2, len(data)):
        raise AssertionError("Invalid GLB header")
    json_size, json_kind = struct.unpack_from("<I4s", data, 12)
    if json_kind != b"JSON" or json_size % 4:
        raise AssertionError("Invalid JSON chunk")
    document = json.loads(data[20:20 + json_size])
    size, kind = struct.unpack_from("<I4s", data, 20 + json_size)
    binary = data[28 + json_size:]
    if kind != b"BIN\x00" or size != len(binary) or size % 4:
        raise AssertionError("Invalid binary chunk")
    return document, binary


def accessor(document, binary, index):
    item = document["accessors"][index]
    view = document["bufferViews"][item["bufferView"]]
    dimensions = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[item["type"]]
    dtype = {5123: "<u2", 5125: "<u4", 5126: "<f4"}[item["componentType"]]
    return np.frombuffer(binary, dtype=dtype, count=item["count"] * dimensions,
                         offset=view.get("byteOffset", 0) + item.get("byteOffset", 0)).reshape(-1, dimensions)


class TreeImpostorTests(unittest.TestCase):
    def test_four_cards_keep_embedded_source_images_and_view_alignment(self):
        for species, height in (("tree", 10.0), ("pine", 14.0)):
            with self.subTest(species=species):
                data = build_impostor(species, height, ASSETS)
                document, binary = read_glb(data)
                self.assertNotIn("uri", document["buffers"][0])
                embedded = []
                for image in document["images"]:
                    self.assertNotIn("uri", image)
                    self.assertEqual(image["mimeType"], "image/png")
                    view = document["bufferViews"][image["bufferView"]]
                    start = view.get("byteOffset", 0)
                    embedded.append(binary[start:start + view["byteLength"]])
                expected = [(ASSETS / f"{species}{suffix}.png").read_bytes()
                            for suffix in ("", "-side", "-top-mid", "-top-two-thirds")]
                self.assertCountEqual(embedded, expected)
                primitives = [p for mesh in document["meshes"] for p in mesh["primitives"]]
                self.assertEqual(len(primitives), 4)
                geometry = []
                for primitive in primitives:
                    attributes = primitive["attributes"]
                    vertices = accessor(document, binary, attributes["POSITION"])
                    normals = accessor(document, binary, attributes["NORMAL"])
                    uv = accessor(document, binary, attributes["TEXCOORD_0"])
                    faces = accessor(document, binary, primitive["indices"]).reshape(-1, 3)
                    self.assertEqual(vertices.shape, (4, 3))
                    self.assertEqual(faces.shape, (2, 3))
                    self.assertTrue(np.isfinite(vertices).all())
                    self.assertTrue(((uv >= 0) & (uv <= 1)).all())
                    edges = vertices[faces[:, 1:]] - vertices[faces[:, :1]]
                    cross = np.cross(edges[:, 0], edges[:, 1])
                    self.assertTrue((np.sum(cross * normals[faces[:, 0]], axis=1) > 0).all())
                    material = document["materials"][primitive["material"]]
                    self.assertEqual(material["alphaMode"], "MASK")
                    self.assertTrue(material["doubleSided"])
                    self.assertIn("KHR_materials_unlit", material["extensions"])
                    geometry.append((vertices, uv))

                front, side, mid, upper = geometry
                np.testing.assert_allclose(front[0][:, 2], 0)
                np.testing.assert_allclose(side[0][:, 0], 0)
                np.testing.assert_allclose(mid[0][:, 1], height / 2, atol=1e-6)
                np.testing.assert_allclose(upper[0][:, 1], height * 2 / 3, atol=1e-6)
                np.testing.assert_allclose(mid[0][:, [0, 2]], upper[0][:, [0, 2]])
                # glTF V=0 is the top image row. Check all camera conventions.
                for (vertices, uv), horizontal_axis, horizontal_sign, vertical_axis, vertical_sign in (
                    (front, 0, 1, 1, -1), (side, 2, -1, 1, -1),
                    (mid, 0, 1, 2, 1), (upper, 0, 1, 2, 1),
                ):
                    u_change = vertices[uv[:, 0] == 1, horizontal_axis].mean() - vertices[uv[:, 0] == 0, horizontal_axis].mean()
                    v_change = vertices[uv[:, 1] == 1, vertical_axis].mean() - vertices[uv[:, 1] == 0, vertical_axis].mean()
                    self.assertGreater(u_change * horizontal_sign, 0)
                    self.assertGreater(v_change * vertical_sign, 0)

                restored = trimesh.load_scene(io.BytesIO(data), file_type="glb", process=False)
                self.assertEqual(sum(len(mesh.faces) for mesh in restored.geometry.values()), 8)
                self.assertEqual(data, build_impostor(species, height, ASSETS))

    def test_invalid_height_is_rejected(self):
        for height in (0, -1, float("nan"), float("inf")):
            with self.subTest(height=height), self.assertRaises(ValueError):
                build_impostor("tree", height, ASSETS)


if __name__ == "__main__":
    unittest.main()
