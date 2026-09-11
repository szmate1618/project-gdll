"""Offline checks for height interpretation, closed roofs, and OSM courtyards."""

import io
import unittest

import numpy as np
from pyproj import Transformer
from shapely import union_all
from shapely.geometry import Polygon, box
import trimesh

from src.buildings import _mesh_for_polygon, add_buildings, building_height, parse_length
from src.osm import parse_osm


class BuildingTests(unittest.TestCase):
    def test_height_precedence_and_units(self):
        config = {"floor_height_m": 3.2, "default_height_m": 8.5}
        self.assertAlmostEqual(parse_length("30 ft"), 9.144)
        self.assertAlmostEqual(parse_length("5' 6\""), 1.6764)
        self.assertEqual(parse_length("12.5 m"), 12.5)
        self.assertIsNone(parse_length("unknown"))
        self.assertEqual(building_height({"height": "12", "building:levels": "8"}, config), 12)
        self.assertAlmostEqual(building_height({"height": "unknown", "building:levels": "3"}, config), 9.6)
        self.assertEqual(building_height({"building": "garage"}, config), 3)
        self.assertEqual(building_height({"building": "yes"}, config), 8.5)

    def test_pitched_roofs_closed_on_slopes_and_preserve_courtyards(self):
        footprints = [
            box(0, 0, 20, 10),
            Polygon([(0, 0), (20, 0), (20, 20), (0, 20)], holes=[[(6, 6), (14, 6), (14, 14), (6, 14)]]),
            Polygon([(0, 0), (20, 0), (20, 8), (8, 8), (8, 20), (0, 20)]),
            # A slightly skewed real OSM outline puts a ridge intersection almost
            # on a foundation sample, exposing floating-point triangulation slivers.
            Polygon([(-487.4009188518976, 630.3056989982724),
                     (-476.3671267540194, 642.4234049450606),
                     (-468.07461269851774, 634.9198497431353),
                     (-479.10863677068846, 622.7910148985684)]),
            # This near-collinear outline creates a legitimate narrow triangle
            # after float32 snapping. Removing it makes a nonmanifold wall edge.
            Polygon([(-13.76327322627185, 43.597916819155216),
                     (-7.049771341844462, 43.488932262174785),
                     (-0.3059653896489181, 43.39043136686087),
                     (-0.9770204349770211, -0.3239571265876293),
                     (-6.307410733599681, -0.2443050500005484),
                     (-14.441946353646927, -0.1163125252351165)]),
        ]
        elevation = lambda x, y: 0.1 * x + 0.2 * y
        for footprint in footprints:
            for shape in ("flat", "gabled", "hipped"):
                with self.subTest(area=footprint.area, shape=shape):
                    wall, roof, _ = _mesh_for_polygon(footprint, {"height": "12", "roof:shape": shape}, elevation, {})
                    solid = trimesh.util.concatenate([wall, roof])
                    solid.merge_vertices(merge_tex=True, merge_norm=True)
                    self.assertTrue(solid.is_watertight)
                    self.assertTrue(solid.is_winding_consistent)
                    self.assertGreater(solid.volume, 0)
                    self.assertTrue(np.all(roof.face_normals[:, 2] > 0))
                    self.assertTrue(np.all(roof.area_faces > 1e-10))
                    # Geometry is intentionally quantized to glTF float32. At
                    # local kilometer coordinates its boundary precision is
                    # about 0.1 mm, so use a precision-derived area tolerance.
                    area_tolerance = max(1e-7, footprint.length * 1e-4)
                    roof_coverage = 0
                    for triangle in roof.triangles:
                        projected = Polygon(triangle[:, :2])
                        self.assertLess(projected.difference(footprint).area, area_tolerance)
                        roof_coverage += projected.area
                    self.assertAlmostEqual(roof_coverage, footprint.area, delta=area_tolerance)
                    lower = wall.vertices[np.isclose(wall.vertices[:, 2], elevation(wall.vertices[:, 0], wall.vertices[:, 1]) - 0.08)]
                    self.assertGreater(len(lower), 0)
                    self.assertTrue(np.allclose(lower[:, 2], elevation(lower[:, 0], lower[:, 1]) - 0.08))

    def test_total_height_and_scene_statistics(self):
        scene = trimesh.Scene()
        feature = {"id": "way/1", "geometry": box(0, 0, 20, 10), "tags": {"height": "12", "roof:shape": "gabled"}}
        stats = add_buildings(scene, [feature], lambda x, y: np.zeros_like(x) + 42, {})
        self.assertEqual(stats["buildings"], 1)
        self.assertEqual(stats["building_meshes"], 2)
        self.assertEqual(stats["roof_shapes"], {"gabled": 1})
        self.assertAlmostEqual(scene.bounds[1, 2], 54)

    def test_gabled_ridge_remains_closed_after_glb_export(self):
        # Three OSM houses in the 6 km map exposed nonmanifold edges after ridge
        # splitting and boundary sampling. Keep projected coordinates to exercise
        # glTF float32 precision, with no dependency on downloads or config.json.
        outlines = {
            296396893: [
                (-220.17259201791603, 1014.9796889070421),
                (-216.76477701897966, 1000.5422920798883),
                (-240.74283371702768, 994.9254750516266),
                (-244.15059406554792, 1009.3628867566586),
            ],
            308361451: [
                (398.5230794965173, 1471.1644005291164),
                (377.81921155919554, 1454.2129898397252),
                (369.3892990175518, 1464.4533852003515),
                (390.0931580614415, 1481.4047660063952),
            ],
            308361465: [
                (592.0372809160617, 1847.258143791929),
                (570.8339020365383, 1829.7051446093246),
                (563.5749315007706, 1838.419569088146),
                (584.7783031476429, 1855.9725419236347),
            ],
        }
        for osm_id, outline in outlines.items():
            with self.subTest(osm_id=osm_id):
                self.check_building_roundtrip(Polygon(outline))

    def check_building_roundtrip(self, footprint):
        elevation = lambda x, y: 0.015 * x + 0.02 * y
        wall, roof, shape = _mesh_for_polygon(footprint, {"building": "house"}, elevation, {})
        self.assertEqual(shape, "gabled")
        self.assertGreater(np.ptp(roof.vertices[:, 2]), 0.5)
        scene = trimesh.Scene()
        scene.add_geometry(wall, geom_name="building_test_walls")
        scene.add_geometry(roof, geom_name="building_test_roof")
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type="glb")), file_type="glb", process=False)
        precision = float(np.spacing(np.float32(np.max(np.abs(footprint.bounds)))))
        area_tolerance = footprint.length * precision
        for phase, model in (("generated", scene), ("GLB round trip", restored)):
            with self.subTest(phase=phase):
                solid = trimesh.util.concatenate(list(model.geometry.values()))
                solid.merge_vertices(merge_tex=True, merge_norm=True)
                edge_counts = np.unique(solid.edges_sorted, axis=0, return_counts=True)[1]
                self.assertTrue(np.all(edge_counts == 2), "Every solid edge must join exactly two faces")
                self.assertTrue(solid.is_watertight)
                self.assertTrue(solid.is_winding_consistent)
                self.assertGreater(solid.volume, 0)
                self.assertTrue(np.all(solid.area_faces > 1e-10))
                model_roof = model.geometry["building_test_roof"]
                self.assertTrue(np.all(model_roof.face_normals[:, 2] > 0))
                projected = [Polygon(triangle[:, :2]) for triangle in model_roof.triangles]
                coverage = union_all(projected)
                self.assertLess(coverage.symmetric_difference(footprint).area, area_tolerance)
                self.assertAlmostEqual(sum(face.area for face in projected), coverage.area, delta=area_tolerance)

    def test_relation_stitching_holes_deduplication_and_road_clipping(self):
        def points(coords):
            return [{"lon": x, "lat": y} for x, y in coords]

        outer_a = [(0, 0), (20, 0), (20, 20)]
        outer_b = [(20, 20), (0, 20), (0, 0)]
        inner = [(6, 6), (14, 6), (14, 14), (6, 14), (6, 6)]
        payload = {"elements": [
            {"type": "way", "id": 1, "tags": {"building": "yes"}, "geometry": points(outer_a)},
            {"type": "way", "id": 2, "tags": {"building": "yes"}, "geometry": points(outer_b)},
            {"type": "way", "id": 3, "tags": {"building": "yes"}, "geometry": points(inner)},
            {"type": "relation", "id": 10, "tags": {"building": "house", "type": "multipolygon"}, "members": [
                {"type": "way", "ref": 1, "role": "outer", "geometry": points(outer_a)},
                {"type": "way", "ref": 2, "role": "outer", "geometry": points(outer_b)},
                {"type": "way", "ref": 3, "role": "inner", "geometry": points(inner)},
            ]},
            {"type": "way", "id": 4, "tags": {"highway": "residential"}, "geometry": points([(-10, 10), (30, 10)])},
        ]}
        transformer = Transformer.from_crs("EPSG:4326", "EPSG:4326", always_xy=True)
        buildings, roads = parse_osm(payload, transformer, (10, 10), box(-10, -10, 10, 10))
        self.assertEqual(len(buildings), 1)
        self.assertEqual(buildings[0]["id"], "relation/10")
        self.assertEqual(len(buildings[0]["geometry"].interiors), 1)
        self.assertAlmostEqual(buildings[0]["geometry"].area, 336)
        self.assertEqual(len(roads), 1)
        self.assertAlmostEqual(roads[0]["geometry"].length, 20)
        self.assertEqual(roads[0]["geometry"].bounds, (-10, 0, 10, 0))


if __name__ == "__main__":
    unittest.main()
