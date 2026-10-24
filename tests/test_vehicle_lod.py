"""Regress normalized frames, mirrored winding, mip memory and texture levels."""
import copy
import io
import os
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
import trimesh
from src.glb import read_glb
from src.vehicle_assets import validated_glb, triangle_count, simplify_static_primitive
from src.vehicle_lod import chassis_model, resize_textures, texture_inventory, generate_levels
from src.vehicle_asset_render import geometry

HELPER = Path(os.environ.get('VEHICLE_SIMPLIFIER', 'build/vehicle_mesh_simplify')).resolve()


def textured(subdivisions=2):
    mesh = trimesh.creation.icosphere(subdivisions=subdivisions)
    image = Image.new('RGBA', (1024, 512), '#667788')
    mesh.visual = trimesh.visual.TextureVisuals(uv=np.zeros((len(mesh.vertices), 2)),
        material=trimesh.visual.material.PBRMaterial(baseColorTexture=image))
    return read_glb(trimesh.Scene(mesh).export(file_type='glb'))


class VehicleLodTests(unittest.TestCase):
    def test_chassis_frame_bakes_parent_reflection_once(self):
        mesh = trimesh.creation.box(extents=[2, 2, 6])
        d,b = read_glb(trimesh.Scene(mesh).export(file_type='glb'))
        d['nodes'] = [{'translation':[18,7,24], 'scale':[-2,1,1], 'children':[1]}, {'mesh':0}]
        d['scenes'] = [{'nodes':[0]}]
        original = copy.deepcopy(d)
        local,data = chassis_model(d,b,180)
        scene = trimesh.load(io.BytesIO(validated_glb(local,data)),file_type='glb',force='scene',process=False)
        np.testing.assert_allclose(scene.bounds, [[-1.2,-.68,-1.8],[1.2,.52,1.8]],atol=1e-5)
        self.assertEqual(triangle_count(local),12)
        self.assertEqual(d,original)
        loaded = geometry(validated_glb(local,data))
        for part in loaded:
            triangles = part['vertices'][part['faces']]
            face = np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0])
            self.assertTrue((np.sum(face*part['normals'][part['faces']].mean(1),axis=1)>0).all())

    def test_legacy_diffuse_is_converted_without_weakening_loader(self):
        d,b = textured()
        d['extensionsRequired'] = ['KHR_materials_pbrSpecularGlossiness']
        d['materials'][0] = {'extensions': {'KHR_materials_pbrSpecularGlossiness': {
            'diffuseFactor': [.2,.4,.6,1], 'diffuseTexture': {'index':0}, 'glossinessFactor':.8}}}
        converted,_ = chassis_model(d,b,0)
        pbr = converted['materials'][0]['pbrMetallicRoughness']
        self.assertEqual(pbr['baseColorFactor'],[.2,.4,.6,1])
        self.assertEqual(pbr['baseColorTexture'],{'index':0})
        self.assertNotIn('KHR_materials_pbrSpecularGlossiness',converted['extensionsRequired'])
        self.assertIn('KHR_materials_pbrSpecularGlossiness',d['extensionsRequired'])

    def test_per_type_length_keeps_authored_proportions_and_ground_pivot(self):
        mesh = trimesh.creation.box(extents=[2, 1.5, 4])
        d,b = read_glb(trimesh.Scene(mesh).export(file_type='glb'))
        for length,expected in [(3.2,[1.6,1.2,3.2]), (5.2,[2.6,1.95,5.2])]:
            fitted,data = chassis_model(d,b,0,length)
            scene = trimesh.load(io.BytesIO(validated_glb(fitted,data)),file_type='glb',force='scene',process=False)
            np.testing.assert_allclose(scene.extents,expected,atol=1e-5)
            self.assertAlmostEqual(scene.bounds[0,1],-.68,places=5)
        for length in [0,-1,float('nan'),float('inf')]:
            with self.assertRaises(ValueError):
                chassis_model(d,b,0,length)

    def test_texture_limits_preserve_aspect_and_exact_mip_allocation(self):
        d,b = textured()
        source = copy.deepcopy(d)
        reduced,data = resize_textures(d,b,128)
        inventory = texture_inventory(reduced,data)
        self.assertEqual([(i['width'],i['height']) for i in inventory['images']],[(128,64)])
        # 128x64 through 2x1, then a 1x1 mip.
        self.assertEqual(inventory['gpu_bytes'],4*(8192+2048+512+128+32+8+2+1))
        self.assertEqual(d,source)
        self.assertEqual(texture_inventory(d,b)['images'][0]['width'],1024)

    @unittest.skipUnless(HELPER.is_file(),'Build vehicle_mesh_simplify first')
    def test_atlas_seams_survive_aggressive_target(self):
        mesh = trimesh.creation.icosphere(subdivisions=0)
        vertices = mesh.vertices[mesh.faces].reshape((-1,3))
        # Every face maps to the same isolated atlas triangle. Collapsing through
        # those seams would pull unrelated texture regions across the surface.
        mesh = trimesh.Trimesh(vertices=vertices,faces=np.arange(len(vertices)).reshape((-1,3)),process=False)
        mesh.visual = trimesh.visual.TextureVisuals(uv=np.tile([[0,0],[1,0],[0,1]],(20,1)),
            material=trimesh.visual.material.PBRMaterial(baseColorTexture=Image.new('RGBA',(8,8),'white')))
        d,b = read_glb(trimesh.Scene(mesh).export(file_type='glb'))
        _,indices = simplify_static_primitive(d,b,d['meshes'][0]['primitives'][0],HELPER,4,True,True,1.)
        self.assertEqual(len(indices)//3,20)

    @unittest.skipUnless(HELPER.is_file(),'Build vehicle_mesh_simplify first')
    def test_four_independent_levels_preserve_material_and_reduce_work(self):
        d,b = textured(3)
        d,b = chassis_model(d,b,0,5.113)
        outputs = generate_levels(d,b,HELPER)
        counts = [stats['triangles'] for _,stats in outputs]
        self.assertEqual(counts[0],1280)
        self.assertTrue(all(a>b for a,b in zip(counts,counts[1:])),counts)
        for level,(data,stats) in enumerate(outputs):
            doc,binary = read_glb(data)
            self.assertEqual(triangle_count(doc),stats['triangles'])
            self.assertTrue(all(max(i['width'],i['height']) <= [1024,512,256,128][level]
                                for i in texture_inventory(doc,binary)['images']))
            self.assertEqual(doc['materials'],d['materials'])
            self.assertEqual(doc['extras']['vehicle_frame']['length_m'],5.113)
            scene = trimesh.load(io.BytesIO(data),file_type='glb',force='scene',process=False)
            self.assertGreater(scene.extents[2],4.8)
            self.assertLess(scene.extents[2],5.4)


if __name__ == '__main__':
    unittest.main()
