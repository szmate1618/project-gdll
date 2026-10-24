"""Regress scene-instance budgets, variant transforms, and preview UV direction."""
import copy
import io
import os
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
import trimesh
from src.glb import read_glb
from src.vehicle_assets import selected_document, simplify_vehicle, triangle_count, validated_glb, merge_identical_solid_materials
from src.vehicle_asset_render import geometry, reference_for, render

HELPER=Path(os.environ.get('VEHICLE_SIMPLIFIER', str(Path(__file__).resolve().parents[1]/'build/vehicle_mesh_simplify')))


def fixture():
    mesh=trimesh.creation.icosphere(subdivisions=2)
    document,binary=read_glb(trimesh.Scene(mesh).export(file_type='glb'))
    matrix=np.array([[-2.,.3,0,1],[0,1.,.2,2],[0,0,1.5,3],[0,0,0,1]])
    document['nodes']=[{'name':'Assembly','translation':[13,7,-4],'children':[1,2]},
                       {'name':'A','mesh':0,'matrix':matrix.ravel(order='F').tolist()},
                       {'name':'B','mesh':0,'translation':[-5,0,0]}]
    document['scenes']=[{'nodes':[0]}]
    document['scene']=0
    return document,binary


class VehicleAssetTests(unittest.TestCase):
    def test_static_material_merge_bakes_each_transform_once(self):
        document,binary=fixture()
        document['materials']=[{'name':'white A','pbrMetallicRoughness':{'baseColorFactor':[1,1,1,1]}},
                               {'name':'white B','pbrMetallicRoughness':{'baseColorFactor':[1,1,1,1]}}]
        document['meshes'][0]['primitives'][0]['material']=0
        document['meshes'].append(copy.deepcopy(document['meshes'][0]))
        document['meshes'][1]['primitives'][0]['material']=1
        document['nodes'][2]['mesh']=1
        original=trimesh.load(io.BytesIO(validated_glb(document,binary)),file_type='glb',force='scene',process=False)
        merged,data=merge_identical_solid_materials(document,binary)
        loaded=trimesh.load(io.BytesIO(validated_glb(merged,data)),file_type='glb',force='scene',process=False)
        np.testing.assert_allclose(loaded.bounds,original.bounds,atol=1e-6)
        self.assertEqual(sum(len(m.faces) for m in loaded.geometry.values()),640)
        self.assertEqual(len(loaded.geometry),1)
        document['materials'][1]['pbrMetallicRoughness']['baseColorFactor']=[0,0,0,1]
        with self.assertRaises(ValueError):
            merge_identical_solid_materials(document,binary)

    def test_variant_compaction_keeps_parent_and_full_node_transform(self):
        document,binary=fixture()
        selected=selected_document(document,'A')
        self.assertEqual(triangle_count(selected),320)
        packed=validated_glb(selected,binary)
        compact,_=read_glb(packed)
        self.assertEqual(len(compact['meshes']),1)
        self.assertEqual({n['name'] for n in compact['nodes']},{'Assembly','A'})
        loaded=trimesh.load(io.BytesIO(packed),file_type='glb',force='scene',process=False)
        self.assertEqual(sum(len(loaded.geometry[loaded.graph[n][1]].faces) for n in loaded.graph.nodes_geometry),320)
        vertices=next(iter(loaded.geometry.values())).vertices
        world=vertices@np.array(document['nodes'][1]['matrix']).reshape((4,4),order='F')[:3,:3].T+np.array([14,9,-1])
        np.testing.assert_allclose(loaded.bounds,np.stack((world.min(0),world.max(0))),atol=1e-6)
        self.assertEqual(document['nodes'][0]['children'],[1,2])

    @unittest.skipUnless(HELPER.is_file(),'Build zombie_mesh_simplify first')
    def test_strict_budget_counts_both_instances_and_keeps_node_frames(self):
        document,binary=fixture()
        nodes=copy.deepcopy(document['nodes'])
        reduced,data,stats=simplify_vehicle(document,binary,HELPER,maximum=99)
        self.assertEqual(stats['before'],640)
        self.assertLessEqual(stats['after'],99)
        self.assertEqual(reduced['nodes'],nodes)
        loaded=trimesh.load(io.BytesIO(validated_glb(reduced,data)),file_type='glb',force='scene',process=False)
        actual=sum(len(loaded.geometry[loaded.graph[n][1]].faces) for n in loaded.graph.nodes_geometry)
        self.assertEqual(actual,stats['after'])
        self.assertLessEqual(actual,99)
        same,same_binary,stats=simplify_vehicle(document,binary,HELPER,maximum=640)
        self.assertFalse(stats['simplified'])
        self.assertEqual(same,document)
        self.assertEqual(same_binary,binary)

    def test_texture_top_row_stays_at_top_of_preview(self):
        texture=np.array([[[1,0,0,1],[0,1,0,1]],[[0,0,1,1],[1,1,0,1]]],dtype=float)
        mesh=dict(vertices=np.array([[-1,-1,0],[1,-1,0],[1,1,0],[-1,1,0]],dtype=float),
                  faces=np.array([[0,1,2],[0,2,3]]), normals=np.tile([0.,0.,1.],(4,1)),
                  uv=np.array([[0,1],[1,1],[1,0],[0,0]],dtype=float),
                  color=np.ones((4,4)),factor=np.ones(4),texture=texture,
                  sampler={'wrapS':33071,'wrapT':33071},alpha='OPAQUE',cutoff=.5,unlit=True)
        reference=(np.zeros(3),np.eye(3),2**.5)
        pixels=np.asarray(render([mesh],reference,(0,0,1),size=(96,96),supersample=1))
        np.testing.assert_array_equal(pixels[32,32],[255,0,0])
        np.testing.assert_array_equal(pixels[32,64],[0,255,0])
        np.testing.assert_array_equal(pixels[64,32],[0,0,255])


if __name__=='__main__':
    unittest.main()
