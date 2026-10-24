"""Deterministic textured CPU previews for static glTF vehicle comparisons.

These are orthographic asset previews, not viewer hardware validation. Normal
maps, environment reflections and PBR specular highlights are not simulated.
"""
from __future__ import annotations
import io
import numpy as np
from PIL import Image
import trimesh
from .glb import read_glb
from .zombie_lod import accessor_array, numeric_attribute


def unit(value):
    return value / np.maximum(np.linalg.norm(value, axis=-1, keepdims=True), 1e-12)


def geometry(data):
    document, binary = read_glb(data)
    images = []
    for spec in document.get('images', []):
        view = document['bufferViews'][spec['bufferView']]
        start = view.get('byteOffset', 0)
        image = Image.open(io.BytesIO(binary[start:start+view['byteLength']])).convert('RGBA')
        images.append(np.asarray(image, dtype=np.float32)/255)
    meshes = []

    def visit(index, parent):
        node = document['nodes'][index]
        if 'matrix' in node:
            local = np.array(node['matrix']).reshape((4, 4), order='F')
        else:
            q = node.get('rotation', [0, 0, 0, 1])
            local = trimesh.transformations.quaternion_matrix([q[3], q[0], q[1], q[2]])
            local[:3, :3] *= np.array(node.get('scale', [1, 1, 1]))[None, :]
            local[:3, 3] = node.get('translation', [0, 0, 0])
        transform = parent @ local
        if 'mesh' in node:
            for p in document['meshes'][node['mesh']]['primitives']:
                attrs = p['attributes']
                positions = numeric_attribute(document, binary, attrs['POSITION'])
                vertices = positions @ transform[:3, :3].T + transform[:3, 3]
                indices = accessor_array(document, binary, p['indices']).reshape((-1, 3)) if 'indices' in p else np.arange(len(vertices)).reshape((-1, 3))
                if 'NORMAL' in attrs:
                    normals = unit(numeric_attribute(document, binary, attrs['NORMAL']) @ np.linalg.inv(transform[:3, :3]))
                else:
                    normals = trimesh.Trimesh(vertices=vertices, faces=indices, process=False).vertex_normals
                color = numeric_attribute(document, binary, attrs['COLOR_0']) if 'COLOR_0' in attrs else np.ones((len(vertices), 4))
                if color.shape[1] == 3:
                    color = np.column_stack((color, np.ones(len(color))))
                material = document.get('materials', [])[p['material']] if 'material' in p else {}
                pbr = material.get('pbrMetallicRoughness', {})
                spec = material.get('extensions', {}).get('KHR_materials_pbrSpecularGlossiness', {})
                factor = np.array(pbr.get('baseColorFactor', spec.get('diffuseFactor', [1, 1, 1, 1])))
                info = pbr.get('baseColorTexture', spec.get('diffuseTexture'))
                texture = None
                sampler = {}
                uv = np.zeros((len(vertices), 2))
                if info:
                    texture_spec = document['textures'][info['index']]
                    texture = images[texture_spec['source']]
                    uv = numeric_attribute(document, binary, attrs['TEXCOORD_'+str(info.get('texCoord', 0))])
                    if 'sampler' in texture_spec:
                        sampler = document.get('samplers', [])[texture_spec['sampler']]
                meshes.append(dict(vertices=vertices, faces=indices, normals=normals, color=color,
                                   factor=factor, uv=uv, texture=texture, sampler=sampler,
                                   alpha=material.get('alphaMode', 'OPAQUE'), cutoff=material.get('alphaCutoff', .5),
                                   unlit='KHR_materials_unlit' in material.get('extensions', {})))
        for child in node.get('children', []):
            visit(child, transform)
    for root in document['scenes'][document.get('scene', 0)]['nodes']:
        visit(root, np.eye(4))
    return meshes


def display_transform(meshes):
    vertices = np.concatenate([m['vertices'] for m in meshes])
    low, high = vertices.min(0), vertices.max(0)
    center = (low+high)/2
    rotation = np.eye(3)
    if high[0]-low[0] > high[2]-low[2]:
        rotation = np.array([[0., 0., -1.], [0., 1., 0.], [1., 0., 0.]])
    return center, rotation


def wrap(value, mode):
    if mode == 33071:
        return np.clip(value, 0, 1-1e-8)
    if mode == 33648:
        return 1-np.abs(np.mod(value, 2)-1)
    return np.mod(value, 1)


def render(meshes, reference, direction, size=(600, 340), supersample=2, wire=False):
    width, height = (n*supersample for n in size)
    center, rotation = reference[:2]
    direction = unit(np.asarray(direction, dtype=float))
    right = unit(np.cross([0, 1, 0], direction))
    up = unit(np.cross(direction, right))
    axes = np.array([right, -up, direction]).T
    all_vertices = np.concatenate([(m['vertices']-center) @ rotation.T for m in meshes])
    # Use a stable bounding sphere from the original asset for every comparison.
    radius = reference[2] if len(reference)>2 else np.max(np.linalg.norm(all_vertices, axis=1))
    scale = min(width/(2.2*radius), height/(1.45*radius))
    background = np.array([.092, .112, .145])
    pixels = np.broadcast_to(background, (height, width, 3)).copy()
    depth = np.full((height, width), -np.inf)
    light = unit(np.array([-.3, .85, 1.0]))
    work = []
    for mesh in meshes:
        vertices = (mesh['vertices']-center) @ rotation.T
        projected = vertices @ axes
        projected[:, :2] = projected[:, :2]*scale + np.array([width/2, height/2])
        normals = unit(mesh['normals'] @ rotation.T)
        for face in mesh['faces']:
            work.append((mesh, face, projected[face], normals[face]))
    opaque = [w for w in work if w[0]['alpha'] != 'BLEND']
    transparent = sorted((w for w in work if w[0]['alpha']=='BLEND'), key=lambda w: float(w[2][:, 2].mean()))
    for mesh, face, points, normals in opaque+transparent:
        x0, y0 = np.maximum(np.floor(points[:, :2].min(0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(points[:, :2].max(0)).astype(int), [width-1, height-1])
        if x1 < x0 or y1 < y0:
            continue
        a,b,c = points[:, :2]
        denominator = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(denominator) < 1e-10:
            continue
        yy,xx = np.mgrid[y0:y1+1, x0:x1+1]
        xx = xx+.5
        yy = yy+.5
        w0 = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
        w1 = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator
        w2 = 1-w0-w1
        z = w0*points[0,2]+w1*points[1,2]+w2*points[2,2]
        region = depth[y0:y1+1,x0:x1+1]
        mask = (w0>=-1e-8)&(w1>=-1e-8)&(w2>=-1e-8)&(z>region)
        if not mask.any():
            continue
        weights = np.stack((w0[mask],w1[mask],w2[mask]), axis=1)
        rgba = (weights @ mesh['color'][face])*mesh['factor']
        if mesh['texture'] is not None:
            uv = weights @ mesh['uv'][face]
            texture = mesh['texture']
            sx = wrap(uv[:,0],mesh['sampler'].get('wrapS',10497))
            sy = wrap(uv[:,1],mesh['sampler'].get('wrapT',10497))
            # glTF UV origin is the upper-left texel in the decoded source image.
            tx = np.minimum((sx*texture.shape[1]).astype(int),texture.shape[1]-1)
            ty = np.minimum((sy*texture.shape[0]).astype(int),texture.shape[0]-1)
            rgba *= texture[ty,tx]
        pixel_normals = unit(weights @ normals)
        pixel_normals *= np.where((pixel_normals @ direction)<0,-1.,1.)[:,None]
        shade = np.ones(len(weights)) if mesh['unlit'] else .46+.54*np.maximum(pixel_normals @ light,0)
        rgb = np.clip(rgba[:,:3]*shade[:,None],0,1)
        if wire:
            # Barycentric distance to each projected edge, in screen pixels.
            altitudes = np.abs(denominator)/np.maximum(np.linalg.norm(np.roll(points[:,:2],-1,axis=0)-np.roll(points[:,:2],-2,axis=0),axis=1),1e-8)
            edges = np.min(weights*altitudes[None,:],axis=1)<.65*supersample
            rgb[edges] *= .25
        alpha = rgba[:,3] if mesh['alpha']=='BLEND' else np.ones(len(weights))
        keep = rgba[:,3]>=mesh['cutoff'] if mesh['alpha']=='MASK' else np.ones(len(weights),dtype=bool)
        ys,xs = np.nonzero(mask)
        ys,xs = ys[keep]+y0,xs[keep]+x0
        pixels[ys,xs] = rgb[keep]*alpha[keep,None] + pixels[ys,xs]*(1-alpha[keep,None])
        if mesh['alpha']!='BLEND':
            depth[ys,xs] = z[mask][keep]
    image = Image.fromarray(np.clip(pixels*255,0,255).astype(np.uint8))
    return image.resize(size,Image.Resampling.LANCZOS)


def reference_for(meshes):
    center, rotation = display_transform(meshes)
    vertices = np.concatenate([m['vertices'] for m in meshes])
    radius = float(np.max(np.linalg.norm(vertices-center,axis=1)))
    return center, rotation, radius
