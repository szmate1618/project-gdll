"""Offline glTF packing and budgeted static vehicle mesh preparation.

Source node transforms and coordinate units stay unchanged. Triangle budgets
count every mesh instance in the selected scene, including mirrored instances.
"""
from __future__ import annotations
import base64
import copy
import json
import mimetypes
from pathlib import Path
import struct
import subprocess
from urllib.parse import unquote
import numpy as np
from .glb import pack_glb, read_glb
from .zombie_lod import accessor_array, numeric_attribute


def simplify_static_primitive(document, binary, primitive, helper, target, textured, lock_borders=False, error_limit=1.0):
    attributes=primitive['attributes']
    positions=numeric_attribute(document,binary,attributes['POSITION'])
    indices=accessor_array(document,binary,primitive['indices']).astype('<u4').ravel()
    values=[]
    weights=[]
    slices={}
    for name,weight in [('NORMAL',.1),('TEXCOORD_0',2.0 if textured else 0.0),('COLOR_0',.5),('TANGENT',0.0)]:
        if name in attributes:
            array=numeric_attribute(document,binary,attributes[name])
            slices[name]=slice(len(weights),len(weights)+array.shape[1])
            values.append(array)
            weights.extend([weight]*array.shape[1])
    if not values:
        values=[np.zeros((len(positions),1),dtype='<f4')]
        weights=[0.0]
    values=np.concatenate(values,axis=1).astype('<f4')
    payload=(struct.pack('<5If',len(positions),len(indices),len(weights),3*target,int(lock_borders),error_limit)+positions.astype('<f4').tobytes()+
             values.tobytes()+np.asarray(weights,dtype='<f4').tobytes()+indices.tobytes())
    if lock_borders:
        # Keep texture-atlas discontinuities while allowing redundant hard-normal
        # splits to collapse. Protect all copies of a position with distinct UVs.
        locks = np.zeros(len(positions), dtype=np.uint8)
        if textured and 'TEXCOORD_0' in attributes:
            uv = numeric_attribute(document, binary, attributes['TEXCOORD_0'])
            _, inverse = np.unique(positions, axis=0, return_inverse=True)
            count = int(inverse.max()) + 1
            low, high = np.full((count, 2), np.inf), np.full((count, 2), -np.inf)
            np.minimum.at(low, inverse, uv)
            np.maximum.at(high, inverse, uv)
            locks[np.any((high - low)[inverse] > 1e-5, axis=1)] = 2 # meshopt_SimplifyVertex_Protect
        payload += locks.tobytes()
    result=subprocess.run([str(helper)],input=payload,capture_output=True,check=True).stdout
    if len(result)<4:
        raise ValueError('Truncated static simplifier output')
    count=struct.unpack_from('<I',result)[0]
    expected=4+positions.nbytes+values.nbytes+count*4
    if len(result)!=expected or count<3 or count%3 or count>len(indices):
        raise ValueError('Invalid static simplifier output')
    updated=np.frombuffer(result,dtype='<f4',count=positions.size,offset=4).reshape(positions.shape)
    attrs=np.frombuffer(result,dtype='<f4',count=values.size,offset=4+positions.nbytes).reshape(values.shape)
    indices=np.frombuffer(result,dtype='<u4',count=count,offset=4+positions.nbytes+values.nbytes)
    if int(indices.max())>=len(positions) or not np.isfinite(updated).all() or not np.isfinite(attrs).all():
        raise ValueError('Invalid updated vertex data')
    used,compact=np.unique(indices,return_inverse=True)
    output={'POSITION':updated[used]}
    for name,section in slices.items():
        array=attrs[used,section].copy()
        if name=='NORMAL':
            array/=np.maximum(np.linalg.norm(array,axis=1,keepdims=True),1e-8)
        output[name]=array
    for name,index in attributes.items():
        if name not in output:
            output[name]=numeric_attribute(document,binary,index)[used]
    return output,compact.astype('<u4')


def embed_gltf(path):
    """Embed local buffers/images without reinterpreting materials or transforms."""
    path = Path(path)
    document = json.loads(path.read_text())
    if document.get('asset', {}).get('version') != '2.0':
        raise ValueError('Expected glTF 2.0')

    def resource(uri):
        if uri.startswith('data:'):
            header, payload = uri.split(',', 1)
            if not header.endswith(';base64'):
                raise ValueError('Expected base64 data URI')
            return base64.b64decode(payload, validate=True)
        resolved = (path.parent / unquote(uri)).resolve()
        if not resolved.is_relative_to(path.parent.resolve()):
            raise ValueError('glTF resource leaves its source directory')
        return resolved.read_bytes()

    binary = bytearray()
    offsets = []
    for buffer in document['buffers']:
        data = resource(buffer['uri'])
        if len(data) != buffer['byteLength']:
            raise ValueError('glTF buffer size mismatch')
        binary.extend(b'\0' * (-len(binary) % 4))
        offsets.append(len(binary))
        binary.extend(data)
    for view in document.get('bufferViews', []):
        view['byteOffset'] = view.get('byteOffset', 0) + offsets[view['buffer']]
        view['buffer'] = 0
    for image in document.get('images', []):
        if 'uri' not in image:
            continue
        uri = image.pop('uri')
        data = resource(uri)
        binary.extend(b'\0' * (-len(binary) % 4))
        image['bufferView'] = len(document['bufferViews'])
        image['mimeType'] = image.get('mimeType') or (
            uri[5:].split(';', 1)[0] if uri.startswith('data:')
            else mimetypes.guess_type(uri)[0])
        if image['mimeType'] not in ('image/png', 'image/jpeg', 'image/webp'):
            raise ValueError('Unrecognized embedded image type')
        document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data)})
        binary.extend(data)
    document['buffers'] = [{'byteLength': len(binary)}]
    return document, bytes(binary)


def selected_document(document, root=None):
    result = copy.deepcopy(document)
    if root is None:
        return result
    matches = [i for i, node in enumerate(result['nodes']) if node.get('name') == root]
    if len(matches) != 1:
        raise ValueError('Variant root must name exactly one node: ' + root)
    wanted = matches[0]

    def contains(index):
        return index == wanted or any(contains(c) for c in result['nodes'][index].get('children', []))

    # Retain the complete ancestor chain: root transforms must be applied once.
    def prune(index):
        if index == wanted:
            return
        node = result['nodes'][index]
        node['children'] = [c for c in node.get('children', []) if contains(c)]
        node.pop('mesh', None)
        for child in node['children']:
            prune(child)

    scene = result['scenes'][result.get('scene', 0)]
    scene['nodes'] = [n for n in scene['nodes'] if contains(n)]
    for node in scene['nodes']:
        prune(node)
    return result


def node_matrix(node):
    if 'matrix' in node:
        return np.array(node['matrix'],dtype=float).reshape((4,4),order='F')
    x,y,z,w=node.get('rotation',[0,0,0,1])
    result=np.eye(4)
    result[:3,:3]=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                   [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                   [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    result[:3,:3]*=np.array(node.get('scale',[1,1,1]))[None,:]
    result[:3,3]=node.get('translation',[0,0,0])
    return result


def mesh_transforms(document):
    transforms = {}
    def visit(index, stack, parent):
        if index in stack:
            raise ValueError('Cyclic glTF node graph')
        node = document['nodes'][index]
        transform=parent@node_matrix(node)
        if 'mesh' in node:
            transforms.setdefault(node['mesh'],[]).append(transform)
        for child in node.get('children', []):
            visit(child, stack | {index}, transform)
    for root in document['scenes'][document.get('scene', 0)]['nodes']:
        visit(root, set(), np.eye(4))
    return transforms


def triangle_count(document):
    count = 0
    for mesh, transforms in mesh_transforms(document).items():
        instances=len(transforms)
        for primitive in document['meshes'][mesh]['primitives']:
            if primitive.get('mode', 4) != 4:
                raise ValueError('Expected triangle primitives')
            index = primitive.get('indices', primitive['attributes']['POSITION'])
            indices = document['accessors'][index]['count']
            if indices % 3:
                raise ValueError('Invalid triangle index count')
            count += instances * (indices // 3)
    return count


def merge_identical_solid_materials(document, binary):
    """Join equivalent static parts so reduction cannot tear their shared edges.

    This intentionally produces a single static mesh, with each source-node
    transform baked exactly once. Original GLBs retain their separate parts.
    """
    import io
    import trimesh
    materials=document.get('materials',[])
    canonical=[{k:v for k,v in m.items() if k!='name'} for m in materials]
    if not canonical or any(m!=canonical[0] for m in canonical) or document.get('textures'):
        raise ValueError('Only identical solid-color materials can be joined')
    if document.get('skins') or document.get('animations'):
        raise ValueError('Only static parts can be joined')
    scene=trimesh.load(io.BytesIO(validated_glb(document,binary)),file_type='glb',force='scene',process=False)
    combined=trimesh.util.concatenate(scene.dump())
    pbr=canonical[0].get('pbrMetallicRoughness',{})
    factor=np.array(pbr.get('baseColorFactor',[1,1,1,1]))*255
    combined.visual=trimesh.visual.TextureVisuals(material=trimesh.visual.material.PBRMaterial(
        baseColorFactor=factor.astype(np.uint8),metallicFactor=pbr.get('metallicFactor',1),
        roughnessFactor=pbr.get('roughnessFactor',1),doubleSided=canonical[0].get('doubleSided',False)))
    result,data=read_glb(trimesh.Scene(combined).export(file_type='glb'))
    result['materials'][0]=copy.deepcopy(materials[0])
    result['asset']=copy.deepcopy(document['asset'])
    result.setdefault('extras',{})['vehicle_preparation']='Merged identical solid materials; source transforms baked once.'
    if triangle_count(result)!=triangle_count(document):
        raise ValueError('Static merge changed triangle count')
    return result,data


def simplify_vehicle(document, original_binary, helper, maximum=999, *, strict_budget=True, lock_borders=False, error_limit=1.0):
    """Keep materials/UVs; strict budgets remain the default for selected meshes.

    LoDs may instead stop at the error or seam-preservation limit. Their measured
    counts, rather than their requested budgets, are published in the manifest.
    """
    before = triangle_count(document)
    if before <= maximum:
        return copy.deepcopy(document), original_binary, {'before': before, 'after': before, 'simplified': False}
    result = copy.deepcopy(document)
    binary = bytearray(original_binary)
    primitives = []
    for mesh, transforms in mesh_transforms(document).items():
        instances=len(transforms)
        for j, primitive in enumerate(document['meshes'][mesh]['primitives']):
            positions = numeric_attribute(document, original_binary, primitive['attributes']['POSITION'])
            indices = accessor_array(document, original_binary, primitive['indices']).ravel()
            triangles = positions[indices].reshape((-1, 3, 3))
            # Budget by rendered area. Accessory nodes can have very different
            # scales from body panels; local mesh areas give them false priority.
            area=0.
            for transform in transforms:
                a=(triangles[:,1]-triangles[:,0])@transform[:3,:3].T
                b=(triangles[:,2]-triangles[:,0])@transform[:3,:3].T
                area+=float(np.linalg.norm(np.cross(a,b),axis=1).sum())/2
            area/=instances
            count = len(indices)//3
            # Geometric area resists spending most of the budget on dense rims.
            weight = max(area, 1e-12)**0.5 * count**0.5
            primitives.append((mesh, j, instances, count, weight))
    minimums = [min(p[3], 6) for p in primitives]
    reserved = sum(p[2]*m for p,m in zip(primitives,minimums))
    if reserved > maximum:
        raise ValueError('More material instances than the triangle budget')
    total_weight = sum(p[2]*p[4] for p in primitives)
    remaining = maximum-reserved
    quotas = [min(p[3], m+int(remaining*p[4]/total_weight)) for p,m in zip(primitives,minimums)]
    # Give spare capacity to surfaces still above budget, honoring instance cost.
    spare = maximum-sum(q*p[2] for p,q in zip(primitives,quotas))
    while spare:
        eligible = [i for i,p in enumerate(primitives) if quotas[i]<p[3] and p[2]<=spare]
        if not eligible:
            break
        i = max(eligible, key=lambda i: primitives[i][4]/quotas[i])
        quotas[i] += 1
        spare -= primitives[i][2]

    def append_accessor(template, array, target):
        binary.extend(b'\0' * (-len(binary) % 4))
        view = len(result['bufferViews'])
        result['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': array.nbytes, 'target': target})
        binary.extend(array.tobytes())
        accessor = copy.deepcopy(template)
        accessor.pop('byteOffset', None)
        accessor.pop('sparse', None)
        accessor.update(bufferView=view, count=len(array))
        if 'min' in accessor or 'max' in accessor:
            accessor['min'], accessor['max'] = array.min(axis=0).tolist(), array.max(axis=0).tolist()
        result['accessors'].append(accessor)
        return len(result['accessors'])-1

    for (mesh, j, instances, count, weight), target in zip(primitives, quotas):
        if count <= target:
            continue
        original = document['meshes'][mesh]['primitives'][j]
        material = document.get('materials', [])[original['material']] if 'material' in original else {}
        # Unused UVs can span hundreds of repeats (notably the Swift). They
        # must not constrain a solid-color material's simplification metric.
        def uses_uv(value):
            if isinstance(value, dict):
                return any((key.endswith('Texture') and isinstance(child, dict) and child.get('texCoord', 0) == 0)
                           or uses_uv(child) for key,child in value.items())
            if isinstance(value, list):
                return any(uses_uv(child) for child in value)
            return False
        attributes, indices = simplify_static_primitive(document, original_binary, original, helper, target, uses_uv(material), lock_borders, error_limit)
        actual = len(indices)//3
        if strict_budget and actual > target:
            raise ValueError(f'Meshoptimizer could not reach budget: {mesh}/{j}: {actual} > {target}')
        primitive = result['meshes'][mesh]['primitives'][j]
        for name, index in list(primitive['attributes'].items()):
            array = attributes[name]
            template=copy.deepcopy(document['accessors'][index])
            template['componentType']=5126
            template.pop('normalized',None)
            primitive['attributes'][name] = append_accessor(template, array, 34962)
        primitive['indices'] = append_accessor({'type': 'SCALAR', 'componentType': 5125}, indices[:, None], 34963)
    result['buffers'][0]['byteLength'] = len(binary)
    after = triangle_count(result)
    if not 0 < after <= before or (strict_budget and after > maximum):
        raise ValueError(f'Vehicle budget failed: {after} > {maximum}')
    result.setdefault('extras', {})['vehicle_simplification'] = {'meshoptimizer': '1.3', 'method': 'simplifyWithUpdate', 'original_triangles': before, 'triangles': after}
    return result, bytes(binary), {'before': before, 'after': after, 'simplified': True}


def validated_glb(document, binary):
    document, binary = compact_document(document, binary)
    data = pack_glb(document, binary)
    read_glb(data)
    return data


def compact_document(document, binary):
    """Discard inactive variants and superseded binary ranges in static assets."""
    result = copy.deepcopy(document)
    if result.get('skins') or result.get('animations'):
        raise ValueError('Vehicle compaction expects static models')
    scene = result['scenes'][result.get('scene', 0)]
    nodes = set()
    def visit(index):
        if index in nodes:
            return
        nodes.add(index)
        for child in result['nodes'][index].get('children', []):
            visit(child)
    for root in scene['nodes']:
        visit(root)

    def select(key, indices):
        mapping = {old: new for new, old in enumerate(sorted(indices))}
        result[key] = [result[key][old] for old in sorted(indices)]
        return mapping

    node_map = select('nodes', nodes)
    result['scenes'] = [copy.deepcopy(scene)]
    result['scene'] = 0
    result['scenes'][0]['nodes'] = [node_map[i] for i in scene['nodes']]
    mesh_map = select('meshes', {n['mesh'] for n in result['nodes'] if 'mesh' in n})
    for node in result['nodes']:
        if 'mesh' in node:
            node['mesh'] = mesh_map[node['mesh']]
        if 'children' in node:
            node['children'] = [node_map[i] for i in node['children']]
    primitives = [p for m in result['meshes'] for p in m['primitives']]
    accessor_map = select('accessors', {i for p in primitives for i in list(p['attributes'].values())+([p['indices']] if 'indices' in p else [])})
    for p in primitives:
        p['attributes'] = {k: accessor_map[i] for k,i in p['attributes'].items()}
        if 'indices' in p:
            p['indices'] = accessor_map[p['indices']]
    material_map = select('materials', {p['material'] for p in primitives if 'material' in p})
    for p in primitives:
        if 'material' in p:
            p['material'] = material_map[p['material']]

    def texture_infos(value):
        if isinstance(value, dict):
            for key,child in value.items():
                if key.endswith('Texture') and isinstance(child, dict) and 'index' in child:
                    yield child
                else:
                    yield from texture_infos(child)
        elif isinstance(value, list):
            for child in value:
                yield from texture_infos(child)

    infos = list(texture_infos(result['materials']))
    texture_map = select('textures', {i['index'] for i in infos})
    for info in infos:
        info['index'] = texture_map[info['index']]
    image_map = select('images', {t['source'] for t in result['textures']})
    for texture in result['textures']:
        texture['source'] = image_map[texture['source']]
    views = {a['bufferView'] for a in result['accessors']} | {i['bufferView'] for i in result['images']}
    view_map = select('bufferViews', views)
    for accessor in result['accessors']:
        accessor['bufferView'] = view_map[accessor['bufferView']]
    for image in result['images']:
        image['bufferView'] = view_map[image['bufferView']]
    packed = bytearray()
    for view in result['bufferViews']:
        start = view.get('byteOffset', 0)
        data = binary[start:start+view['byteLength']]
        if len(data) != view['byteLength']:
            raise ValueError('Invalid source buffer view')
        packed.extend(b'\0' * (-len(packed) % 4))
        view['byteOffset'] = len(packed)
        packed.extend(data)
    result['buffers'] = [{'byteLength': len(packed)}]
    return result, bytes(packed)
