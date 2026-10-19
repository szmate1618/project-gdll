"""The asymmetric crown joins the leaning trunk above ground, without a second root."""

import unittest

import numpy as np
from PIL import Image

from test_tree_impostors import accessor, read_glb
from tools.create_dry_tree_impostor import ASSET_DIR, build_impostor


class DryTreeImpostorTests(unittest.TestCase):
    def test_offset_crown_joins_visible_wood_and_has_no_lower_trunk(self):
        data = build_impostor()
        document, binary = read_glb(data)
        metadata = document["extras"]
        self.assertEqual(metadata["visibleHeightMeters"], 5)
        join = metadata["branchJoinMeters"]
        self.assertLess(join[0], -1)
        self.assertGreater(join[1], 2.5)
        sources = metadata["sourceTextures"]
        front, branches = sources
        root_x, _ = front["rootPixels"]
        self.assertGreater(root_x / front["canvasPixels"][0], .65)
        self.assertLess(root_x / front["canvasPixels"][0], .80)
        for source, anchor in ((front, "branchJoinPixels"), (branches, "joinPixels")):
            with Image.open(ASSET_DIR / source["file"]) as image:
                self.assertGreaterEqual(image.getpixel(tuple(source[anchor]))[3], 90)
        primitives = document["meshes"][0]["primitives"]
        self.assertEqual(len(primitives), 2)
        for i, primitive in enumerate(primitives):
            vertices = accessor(document, binary, primitive["attributes"]["POSITION"])
            normals = accessor(document, binary, primitive["attributes"]["NORMAL"])
            uv = accessor(document, binary, primitive["attributes"]["TEXCOORD_0"])
            faces = accessor(document, binary, primitive["indices"]).reshape(-1, 3)
            cross = np.cross(vertices[faces[:, 1]] - vertices[faces[:, 0]],
                             vertices[faces[:, 2]] - vertices[faces[:, 0]])
            self.assertTrue((np.sum(cross * normals[faces[:, 0]], axis=1) > 0).all())
            self.assertTrue(np.isfinite(vertices).all())
            self.assertTrue(((uv >= 0) & (uv <= 1)).all())
            if i == 0:
                np.testing.assert_allclose(vertices[:, 2], 0)
            else:
                np.testing.assert_allclose(vertices[:, 0], join[0])
                self.assertGreater(vertices[:, 1].min(), 1.5)
            # The painted joins must land on the same point on both cards.
            anchor = front["branchJoinPixels"] if i == 0 else branches["joinPixels"]
            u, v = np.array(anchor) / np.array(sources[i]["canvasPixels"])
            point = vertices[0] + u * (vertices[3] - vertices[0]) + v * (vertices[1] - vertices[0])
            np.testing.assert_allclose(point, join, atol=1e-6)
            material = document["materials"][i]
            self.assertEqual(material["alphaMode"], "MASK")
            self.assertTrue(material["doubleSided"])
            self.assertIn("KHR_materials_unlit", material["extensions"])
        self.assertEqual(len(document["images"]), 2)
        for source, image in zip(sources, document["images"]):
            view = document["bufferViews"][image["bufferView"]]
            start = view["byteOffset"]
            self.assertEqual(binary[start:start + view["byteLength"]],
                             (ASSET_DIR / source["file"]).read_bytes())
        self.assertEqual(data, build_impostor())

    def test_invalid_height(self):
        for height in (0, -1, float("inf"), float("nan")):
            with self.subTest(height=height), self.assertRaises(ValueError):
                build_impostor(height=height)


if __name__ == "__main__":
    unittest.main()
