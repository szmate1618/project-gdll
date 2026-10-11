"""Offline facade openings, road facing, and both exported building LoDs."""

import io
import json
import struct
import unittest

import numpy as np
from shapely import union_all
from shapely.affinity import rotate, translate
from shapely.geometry import LineString, MultiPolygon, Polygon, box
import trimesh

from src.building_details import RoadIndex, _door_facades, _facades
from src.buildings import _mesh_for_polygon, add_buildings
from src.wall_openings import _FacadeMatcher


def panels(mesh):
    mesh = mesh.copy()
    mesh.merge_vertices(merge_tex=True, merge_norm=True)
    return list(mesh.split(only_watertight=False, repair=False))


class BuildingDetailTests(unittest.TestCase):
    def generate(self, polygon, roads=(), tags=None, elevation=None):
        scene = trimesh.Scene()
        feature = {'id': 'way/1', 'geometry': polygon,
                   'tags': tags or {'height': '9', 'roof:shape': 'flat'}}
        elevation = elevation or (lambda x, y: np.zeros_like(x))
        stats = add_buildings(scene, [feature], elevation, {}, roads=roads)
        return scene, stats

    def assert_closed_lods(self, scene, prefix='building_way_1_0'):
        low = trimesh.util.concatenate([scene.geometry[prefix + '_walls'], scene.geometry[prefix + '_roof']])
        near = trimesh.util.concatenate([scene.geometry[prefix + suffix] for suffix in
                                         ('_detailed_walls', '_roof', '_doors', '_windows')
                                         if prefix + suffix in scene.geometry])
        for solid in (low, near):
            solid.merge_vertices(merge_tex=True, merge_norm=True)
            self.assertTrue(solid.is_watertight)
            self.assertTrue(solid.is_winding_consistent)
            self.assertGreater(solid.volume, 0)
            self.assertTrue(np.all(solid.area_faces > 1e-10))
        self.assertAlmostEqual(low.volume, near.volume, delta=max(1e-5, low.volume * 1e-5))

    def test_nearest_road_facing_and_opposite_doors_on_rotated_footprint(self):
        polygon = rotate(box(0, 0, 12, 9), 31, origin=(0, 0))
        near = rotate(LineString([(-100, -3), (100, -3)]), 31, origin=(0, 0))
        far = rotate(LineString([(0, 20), (12, 20)]), 31, origin=(0, 0))
        roads = [{'geometry': far}, {'geometry': near}]
        self.assertTrue(RoadIndex(roads).nearest(polygon).equals(near))
        scene, stats = self.generate(polygon, roads)
        self.assertEqual(stats['doors'], 2)
        doors = panels(scene.geometry['building_way_1_0_doors'])
        self.assertEqual(len(doors), 2)
        south = rotate(LineString([(0, 0), (12, 0)]), 31, origin=(0, 0))
        north = rotate(LineString([(0, 9), (12, 9)]), 31, origin=(0, 0))
        distances = sorted(min(side.distance(LineString(door.vertices[:, :2])) for door in doors)
                           for side in (south, north))
        np.testing.assert_allclose(distances, 0, atol=1e-6)
        np.testing.assert_allclose(doors[0].face_normals[0], -doors[1].face_normals[0], atol=1e-6)

    def test_equally_close_corner_walls_prefer_road_facing_normal(self):
        polygon = box(0, 0, 12, 9)
        # The road ends beside the SE corner. South and east walls have the
        # same nearest-point distance, but the south wall faces it directly.
        road = LineString([(12.2, -3), (20, -3)])
        facades = _facades(polygon)
        front, rear = _door_facades(facades, road)
        np.testing.assert_allclose(facades[front].normal, [0, -1])
        np.testing.assert_allclose(facades[rear].normal, [0, 1])
        # Permit small distance differences rather than exact numeric equality.
        road = LineString([(12.2, -3), (20, -2.8)])
        front, _ = _door_facades(facades, road)
        np.testing.assert_allclose(facades[front].normal, [0, -1])

    def test_windows_cover_outer_and_courtyard_walls_with_real_holes(self):
        polygon = Polygon([(0, 0), (20, 0), (20, 20), (0, 20)],
                          holes=[[(6, 6), (14, 6), (14, 14), (6, 14)]])
        scene, stats = self.generate(polygon)
        windows = scene.geometry['building_way_1_0_windows']
        self.assertGreater(stats['windows'], 20)
        self.assertEqual(len(panels(windows)), stats['windows'])
        centers = np.array([panel.vertices.mean(axis=0) for panel in panels(windows)])
        for axis, coordinate in [(0, 0), (0, 20), (1, 0), (1, 20),
                                 (0, 6), (0, 14), (1, 6), (1, 14)]:
            self.assertTrue(np.any(np.isclose(centers[:, axis], coordinate)))
        masonry = scene.geometry['building_way_1_0_detailed_walls']
        # For every infill triangle, independently intersect all coplanar wall
        # triangles. No opaque masonry may remain behind glass or doors.
        for suffix in ('_doors', '_windows'):
            for infill, normal in zip(scene.geometry['building_way_1_0' + suffix].triangles,
                                      scene.geometry['building_way_1_0' + suffix].face_normals):
                axis = int(np.argmax(np.abs(normal[:2])))
                plane = infill[:, axis].mean()
                projection = [1 - axis, 2]
                aperture = Polygon(infill[:, projection])
                for triangle in masonry.triangles:
                    if np.allclose(triangle[:, axis], plane, atol=1e-6):
                        self.assertLess(aperture.intersection(Polygon(triangle[:, projection])).area, 1e-7)
        self.assert_closed_lods(scene)

    def test_sloped_ground_pitched_roof_and_unchanged_low_lod(self):
        polygon = translate(rotate(box(0, 0, 20, 12), 17), 1500, -1700)
        elevation = lambda x, y: .1 * x + .08 * y
        for shape in ('flat', 'gabled', 'hipped'):
            tags = {'height': '9', 'roof:shape': shape}
            scene, _ = self.generate(polygon, tags=tags, elevation=elevation)
            wall, roof, _ = _mesh_for_polygon(polygon, tags, elevation, {})
            np.testing.assert_array_equal(scene.geometry['building_way_1_0_walls'].vertices, wall.vertices)
            np.testing.assert_array_equal(scene.geometry['building_way_1_0_roof'].vertices, roof.vertices)
            for suffix in ('doors', 'windows'):
                mesh = scene.geometry[f'building_way_1_0_{suffix}']
                self.assertLess(mesh.vertices[:, 2].max(), roof.vertices[:, 2].min())
                ground = elevation(mesh.vertices[:, 0], mesh.vertices[:, 1])
                if suffix == 'windows':
                    self.assertTrue(np.all(mesh.vertices[:, 2] > ground))
                else:
                    self.assertAlmostEqual(np.min(mesh.vertices[:, 2] - ground), -.08, delta=1e-4)
            self.assert_closed_lods(scene)
            restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
            self.assert_closed_lods(restored)

    def test_concave_and_real_osm_outline_roundtrip(self):
        outlines = [Polygon([(0, 0), (20, 0), (20, 8), (8, 8), (8, 20), (0, 20)]),
                    Polygon([(-13.76327322627185, 43.597916819155216),
                             (-7.049771341844462, 43.488932262174785),
                             (-0.3059653896489181, 43.39043136686087),
                             (-0.9770204349770211, -0.3239571265876293),
                             (-6.307410733599681, -0.2443050500005484),
                             (-14.441946353646927, -0.1163125252351165)])]
        for polygon in outlines:
            for shape in ('flat', 'gabled', 'hipped'):
                scene, _ = self.generate(polygon, tags={'height': '9', 'roof:shape': shape},
                                         elevation=lambda x, y: .1 * x + .08 * y)
                self.assert_closed_lods(scene)
                restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
                self.assert_closed_lods(restored)

    def test_steep_bottom_caps_are_refined_without_changing_the_surface(self):
        scene, _ = self.generate(box(0, 0, 12, 9), elevation=lambda x, y: 2.5 * x + 3.0 * y)
        low = scene.geometry['building_way_1_0_walls']
        near = scene.geometry['building_way_1_0_detailed_walls']
        low_caps = low.triangles[low.face_normals[:, 2] < -1e-6].astype(np.float32)
        near_caps = near.triangles[near.face_normals[:, 2] < -1e-6].astype(np.float32)
        self.assertGreater(len(low_caps), 0)
        self.assertTrue(np.all(low.face_normals[low.face_normals[:, 2] < -1e-6, 2] > -.5))
        self.assertGreater(len(near_caps), len(low_caps))
        near_coverage = union_all([Polygon(triangle[:, :2]) for triangle in near_caps])
        low_coverage = union_all([Polygon(triangle[:, :2]) for triangle in low_caps])
        self.assertLess(near_coverage.symmetric_difference(low_coverage).area, 1e-7)
        np.testing.assert_allclose(near_caps[:, :, 2], 2.5 * near_caps[:, :, 0] + 3 * near_caps[:, :, 1] - .08,
                                   atol=1e-5, rtol=0)
        self.assertTrue(set(map(tuple, low_caps.reshape(-1, 3))).issubset(map(tuple, near_caps.reshape(-1, 3))))
        self.assert_closed_lods(scene)

    def test_quantized_roof_split_edge_can_have_a_different_normal(self):
        # way/184191061 fails a normal-only facade match: two roof boundary
        # points are 15 micrometers apart after GLB float32 quantization.
        polygon = Polygon([
            (-1184.8570124516264, 82.2104016225785),
            (-1189.205463159189, 76.86654109787196),
            (-1186.3960587715264, 74.60495015326887),
            (-1182.0320992218913, 79.97071772906929),
            (-1176.3989654409816, 75.41388225741684),
            (-1185.0185559689417, 64.82456346787512),
            (-1199.419267963036, 76.47365352790803),
            (-1195.3257705303258, 81.50052837654948),
            (-1194.8525259784656, 81.12348867673427),
            (-1190.3496869633673, 86.65305458568037),
        ])
        scene, _ = self.generate(polygon, tags={'building': 'house', 'height': '12'})
        wall = scene.geometry['building_way_1_0_walls']
        facades = _facades(polygon)
        unusual = [(triangle, normal) for triangle, normal in zip(wall.triangles, wall.face_normals)
                   if abs(normal[2]) <= 1e-6 and not any(np.dot(normal[:2], facade.normal) > .99 for facade in facades)]
        self.assertGreater(len(unusual), 0, 'Fixture must exercise the rotated tiny boundary edge')
        self.assert_closed_lods(scene)
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
        self.assert_closed_lods(restored)

    def test_matching_facade_rejects_distant_parallel_wall(self):
        matcher = _FacadeMatcher(_facades(box(0, 0, 10, 10)))
        with self.assertRaisesRegex(ValueError, 'Cannot associate building wall'):
            matcher.match(np.array([[30., 0.], [35., 0.]]), np.array([0., -1., 0.]))

    def test_door_base_near_pitched_roof_split_stays_manifold(self):
        # way/308362429 has two roof/foundation samples only one float32
        # horizontal step apart. The door reaches their foundation profile.
        polygon = Polygon([
            (-18.737089403090067, 2068.7768198419362),
            (-25.26470115716802, 2062.655644873157),
            (-35.462605126725975, 2073.467895573005),
            (-28.934993641159963, 2079.5890591954812),
        ])
        road = LineString([(-38, 2076), (-32, 2083)])
        scene, stats = self.generate(polygon, roads=[{'geometry': road}], tags={'building': 'house'},
                                     elevation=lambda x, y: .05 * x + .1 * y)
        self.assertEqual(stats['doors'], 2)
        self.assert_closed_lods(scene)
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
        self.assert_closed_lods(restored)

    def test_window_edge_rounds_onto_foundation_split_without_cracking(self):
        # way/308352807: a window edge and a foundation sample coincide only
        # after float32 rounding. Clipping first leaves a collapsed sliver.
        polygon = Polygon([
            (1460.9602589193382, 906.0711084213108),
            (1446.574751331471, 895.254283465445),
            (1439.3897018110147, 904.7442410849035),
            (1453.7754339554813, 915.5721617089584),
        ])
        scene, _ = self.generate(polygon, tags={'building': 'house'})
        self.assert_closed_lods(scene)
        restored = trimesh.load_scene(io.BytesIO(scene.export(file_type='glb')), file_type='glb', process=False)
        self.assert_closed_lods(restored)

    def test_tiny_original_facade_segments_are_retained(self):
        polygon = Polygon([(0, 0), (10, 0), (10, 10), (9.995, 10.005), (0, 10)])
        facades = _facades(polygon)
        self.assertTrue(any(0 < facade.length < .01 for facade in facades))
        scene, _ = self.generate(polygon)
        self.assert_closed_lods(scene)

    def test_door_bottom_follows_foundation_samples_across_a_terrain_dip(self):
        # The rendered wall base is linear between 6 m foundation samples,
        # while the terrain dips more sharply beneath the centered doors.
        scene, _ = self.generate(box(0, 0, 12, 9), elevation=lambda x, y: (x - 6) ** 2)
        doors = scene.geometry['building_way_1_0_doors']
        south = doors.vertices[np.isclose(doors.vertices[:, 1], 0)]
        bottom = south[south[:, 2] < 3]
        np.testing.assert_allclose(bottom[np.argsort(bottom[:, 0])],
                                   [[5.5, 0, 2.92], [6, 0, -.08], [6.5, 0, 2.92]], atol=1e-6)
        self.assert_closed_lods(scene)

    def test_window_rectangles_use_four_vertices_and_two_triangles(self):
        scene, stats = self.generate(box(0, 0, 12, 9))
        windows = scene.geometry['building_way_1_0_windows']
        self.assertEqual(len(windows.vertices), stats['windows'] * 4)
        self.assertEqual(len(windows.faces), stats['windows'] * 2)
        for window in panels(windows):
            self.assertEqual(len(window.vertices), 4)
            self.assertEqual(len(window.faces), 2)
        for suffix in ('detailed_walls', 'doors', 'windows'):
            mesh = scene.geometry['building_way_1_0_' + suffix]
            attributes = np.column_stack([mesh.vertices.astype(np.float32), mesh.vertex_normals.astype(np.float32)])
            self.assertEqual(len(np.unique(attributes, axis=0)), len(mesh.vertices))
        self.assert_closed_lods(scene)

    def test_first_window_row_and_doors_share_the_same_head_height(self):
        for elevation in (lambda x, y: np.zeros_like(x), lambda x, y: .1 * x + .2 * y):
            scene, _ = self.generate(box(0, 0, 12, 9), elevation=elevation)
            doors = panels(scene.geometry['building_way_1_0_doors'])
            windows = panels(scene.geometry['building_way_1_0_windows'])
            first_head = min(window.bounds[1, 2] for window in windows)
            for door in doors:
                self.assertEqual(door.bounds[1, 2], first_head)
            self.assert_closed_lods(scene)

    def test_export_metadata_color_alpha_and_coordinate_frame(self):
        scene, stats = self.generate(box(0, 0, 12, 9))
        rotation = np.array([[1., 0., 0., 0.], [0., 0., 1., 0.], [0., -1., 0., 0.], [0., 0., 0., 1.]])
        scene.apply_transform(rotation)
        data = scene.export(file_type='glb')
        size = struct.unpack_from('<I', data, 12)[0]
        doc = json.loads(data[20:20 + size])
        details = [mesh for mesh in doc['meshes'] if 'building_detail' in mesh.get('extras', {})]
        self.assertEqual(len(details), 4)
        self.assertEqual(stats['building_meshes'], 2)
        self.assertEqual(stats['building_detail_meshes'], 3)
        self.assertEqual(sorted(mesh['extras']['building_detail']['level'] for mesh in details),
                         ['far', 'near', 'near', 'near'])
        for mesh in details:
            detail = mesh['extras']['building_detail']
            self.assertEqual(detail['version'], 2)
            self.assertEqual(detail['distance_m'], 120)
            self.assertEqual(detail['group'], 'building_way_1_0')
        materials = {material['name']: material for material in doc['materials']}
        window = materials['building_window_blue']
        self.assertEqual(window['alphaMode'], 'BLEND')
        self.assertTrue(window['doubleSided'])
        self.assertTrue(materials['building_door_brown']['doubleSided'])
        self.assertTrue(materials['generic_roof']['doubleSided'])
        r, g, b, opacity = window['pbrMetallicRoughness']['baseColorFactor']
        self.assertLess(r, g)
        self.assertLess(g, b)
        self.assertAlmostEqual(opacity, .4)
        door = materials['building_door_brown']['pbrMetallicRoughness']['baseColorFactor']
        self.assertGreater(door[0], door[1])
        self.assertGreater(door[1], door[2])
        self.assertEqual(door[3], 1)
        restored = trimesh.load_scene(io.BytesIO(data), file_type='glb', process=False)
        windows = restored.geometry['building_way_1_0_windows']
        node = next(node for node in restored.graph.nodes_geometry
                    if restored.graph[node][1] == 'building_way_1_0_windows')
        world = trimesh.transform_points(windows.vertices, restored.graph[node][0])
        self.assertGreater(world[:, 1].min(), 0)
        self.assertLessEqual(world[:, 2].min(), -9)

    def test_tiny_buildings_and_multipolygon_without_roads(self):
        scene, stats = self.generate(MultiPolygon([box(0, 0, 1.5, 1.5), box(10, 0, 22, 9)]))
        self.assertEqual(stats['buildings'], 1)
        self.assertEqual(stats['doors'], 2)
        self.assertNotIn('building_way_1_0_doors', scene.geometry)
        self.assertNotIn('building_detail', scene.geometry['building_way_1_0_walls'].metadata)
        self.assertTrue(np.isfinite(scene.bounds).all())


if __name__ == '__main__':
    unittest.main()
