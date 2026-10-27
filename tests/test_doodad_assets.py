"""Check the requested primitive shapes in the actual exported float32 meshes."""
from collections import Counter
import unittest

import numpy as np
import trimesh

from src.doodad_geometry import models
from src.vehicle_asset_render import geometry
from src.doodad_assets import build_glb


def exported_mesh(model):
    meshes = geometry(build_glb(model))
    solid = trimesh.util.concatenate([
        trimesh.Trimesh(vertices=m["vertices"], faces=m["faces"], process=False)
        for m in meshes])
    solid.merge_vertices()
    return solid


class DoodadAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = {model["id"]: exported_mesh(model) for model in models()}

    def test_exports_are_closed_outward_solids_with_ground_origins(self):
        for name, mesh in self.assets.items():
            with self.subTest(asset=name):
                self.assertTrue(mesh.is_watertight)
                self.assertTrue(mesh.is_winding_consistent)
                self.assertGreater(mesh.volume, 0)
                self.assertTrue(np.isfinite(mesh.vertices).all())
                self.assertTrue((mesh.area_faces > 1e-10).all())
                self.assertAlmostEqual(mesh.bounds[0, 1], 0)
                self.assertEqual(len(mesh.split()), 1)
                self.assertEqual(mesh.euler_number, 2)

    def test_branch_has_concave_nine_corner_base(self):
        mesh = self.assets["branch"]
        bottom = mesh.vertices[np.isclose(mesh.vertices[:, 1], 0)]
        self.assertEqual(len(bottom), 9)
        base_faces = mesh.faces[np.all(np.isclose(mesh.vertices[mesh.faces, 1], 0), axis=1)]
        self.assertEqual(len(base_faces), 7)
        edges = Counter(tuple(sorted((int(a), int(b)))) for face in base_faces
                        for a, b in zip(face, np.roll(face, -1)))
        boundary = [edge for edge, uses in edges.items() if uses == 1]
        self.assertEqual(len(boundary), 9)
        # The projected base occupies less area than its hull: the Y fork is open.
        from scipy.spatial import ConvexHull
        hull_area = ConvexHull(bottom[:, [0, 2]]).volume
        base_area = mesh.area_faces[np.all(np.isclose(mesh.vertices[mesh.faces, 1], 0), axis=1)].sum()
        self.assertLess(base_area, hull_area*.8)

    def test_stone_has_only_two_pentagonal_rings(self):
        mesh = self.assets["stone"]
        self.assertEqual(len(mesh.vertices), 10)
        self.assertEqual(len(mesh.faces), 16)
        levels, counts = np.unique(mesh.vertices[:, 1], return_counts=True)
        self.assertEqual(counts.tolist(), [5, 5])
        bottom = mesh.vertices[mesh.vertices[:, 1] == levels[0]][:, [0, 2]]
        top = mesh.vertices[mesh.vertices[:, 1] == levels[1]][:, [0, 2]]
        self.assertLess(np.ptp(top, axis=0).max(), np.ptp(bottom, axis=0).max())

    def test_branch_has_six_planar_roof_slopes(self):
        mesh = self.assets["branch"]
        # Vertical cut faces can acquire tiny Y components at float32 precision.
        roof = np.flatnonzero(mesh.face_normals[:, 1] > 1e-5)
        self.assertEqual(len(roof), 12)
        # Each pair of triangles must describe one flat slope. A twisted quad
        # makes an extra kink in a limb's otherwise triangular cross-section.
        for index in roof:
            partners = [other for other in roof if other != index
                        and len(set(mesh.faces[index]) & set(mesh.faces[other])) == 2
                        and np.allclose(mesh.face_normals[index], mesh.face_normals[other], atol=1e-6)]
            self.assertEqual(len(partners), 1)

    def test_bottle_is_two_hexagonal_columns_joined_by_one_frustum(self):
        mesh = self.assets["beer_bottle"]
        self.assertEqual(len(mesh.faces), 44)
        levels, counts = np.unique(mesh.vertices[:, 1], return_counts=True)
        self.assertEqual(counts.tolist(), [6, 6, 6, 6])
        rings = [mesh.vertices[mesh.vertices[:, 1] == y][:, [0, 2]] for y in levels]
        radii = [np.linalg.norm(ring, axis=1).mean() for ring in rings]
        self.assertAlmostEqual(radii[0], radii[1])
        self.assertAlmostEqual(radii[2], radii[3])
        self.assertGreater(radii[1], radii[2]*1.5)
        self.assertAlmostEqual(float(levels[-1]), .25, places=5)


if __name__ == "__main__":
    unittest.main()
