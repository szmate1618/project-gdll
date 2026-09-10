import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image
import trimesh

from src.terrain import Terrain, fill_missing
from src.config import coordinate_frame, load_config, ROOT
from src.verify import inspect_glb


class TerrainTests(unittest.TestCase):
    def test_sampler_matches_triangle_plane_not_bilinear_saddle(self):
        terrain = Terrain(np.array([[0., 0.], [0., 10.]]), (0, 0, 1, 1))
        self.assertAlmostEqual(terrain(.5, .5), 5)
        self.assertAlmostEqual(terrain(.75, .75), 2.5)
        self.assertAlmostEqual(terrain(.25, .25), 2.5)
        self.assertAlmostEqual(terrain(1, 0), 10)

    def test_normals_uv_and_embedded_texture_roundtrip(self):
        terrain = Terrain(np.zeros((3, 4)), (-3, -2, 3, 2))
        image = Image.new("RGB", (4, 4), (40, 120, 60))
        mesh = terrain.mesh(image)
        self.assertTrue((mesh.face_normals[:, 2] > .99).all())
        np.testing.assert_allclose(mesh.visual.uv[0], [0, 1])  # NW corner
        np.testing.assert_allclose(mesh.visual.uv[-1], [1, 0])  # SE corner
        (ROOT / "data/tmp").mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "data/tmp") as directory:
            path = Path(directory) / "scene.glb"
            trimesh.Scene(mesh).export(path)
            report = inspect_glb(path)
            self.assertEqual(report["embedded_images"], 1)
            restored = next(iter(trimesh.load_scene(path, process=False).geometry.values()))
            np.testing.assert_allclose(restored.visual.uv, mesh.visual.uv, atol=1e-6)

    def test_missing_data_fills_only_when_most_data_exists(self):
        array = np.arange(16, dtype=float).reshape(4, 4)
        array[1, 1] = np.nan
        self.assertTrue(np.isfinite(fill_missing(array, np.isfinite(array), "test")).all())
        with self.assertRaises(ValueError):
            fill_missing(np.zeros((4, 4)), np.zeros((4, 4), dtype=bool), "test")

    def test_origin_and_metric_dimensions(self):
        config = load_config(ROOT / "config.json")
        frame = coordinate_frame(config)
        np.testing.assert_allclose(frame["transformer"].transform(*frame["center"]), frame["origin"])
        xmin, ymin, xmax, ymax = frame["bounds"]
        self.assertTrue(1900 < xmax - xmin < 2200)
        self.assertTrue(1900 < ymax - ymin < 2200)


if __name__ == "__main__":
    unittest.main()
