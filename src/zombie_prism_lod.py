"""Rig-preserving zombie LODs made of ten open triangular columns.

Fitting uses the source mesh's bind-space units (centimeters for these
characters), recovered joint frames, and vertices grouped by dominant skin joint.
The original scene nodes still perform the sole conversion into runtime meters.
"""
from __future__ import annotations

import copy
import hashlib
import io
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

from .glb import pack_glb, read_glb
from .zombie_lod import accessor_array, numeric_attribute


def production_prism_color_mode(source_name):
    """Keep authored clothing colors, with the verified Male C vest override.

    Male C's end-ring samples miss its off-white vest. Its mid-torso neutral
    samples recover that color; applying the same white policy to other outfits
    would erase their authored palette.
    """
    return 'white_vest' if Path(source_name).stem == 'ZombieMale_C' else 'sampled'


def unit(vector):
    length = np.linalg.norm(vector)
    if not np.isfinite(length) or length < 1e-8:
        raise ValueError('Degenerate prism direction')
    return vector / length


def source_colors(document, binary, primitive):
    """Sample the authored base color into linear RGB for vertex-color baking."""
    attributes = primitive['attributes']
    count = document['accessors'][attributes['POSITION']]['count']
    material = document['materials'][primitive['material']]
    pbr = material.get('pbrMetallicRoughness', {})
    colors = np.tile(np.asarray(pbr.get('baseColorFactor', [1, 1, 1, 1]), dtype=np.float64), (count, 1))
    if 'baseColorTexture' in pbr:
        texture = document['textures'][pbr['baseColorTexture']['index']]
        image = document['images'][texture['source']]
        if 'bufferView' not in image:
            raise ValueError('Prism LOD generation requires an embedded source texture')
        view = document['bufferViews'][image['bufferView']]
        start = view.get('byteOffset', 0)
        rgba = np.asarray(Image.open(io.BytesIO(binary[start:start + view['byteLength']])).convert('RGBA'))
        uv = numeric_attribute(document, binary, attributes['TEXCOORD_0'])
        # Match the glTF native image orientation used by the viewer (no flip).
        x = np.floor((uv[:, 0] % 1) * rgba.shape[1]).astype(int)
        y = np.floor((uv[:, 1] % 1) * rgba.shape[0]).astype(int)
        sampled = rgba[y, x].astype(np.float64) / 255
        sampled[:, :3] = np.where(sampled[:, :3] <= .04045, sampled[:, :3] / 12.92,
                                  ((sampled[:, :3] + .055) / 1.055) ** 2.4)
        colors *= sampled
    if 'COLOR_0' in attributes:
        authored = numeric_attribute(document, binary, attributes['COLOR_0'])
        colors[:, :authored.shape[1]] *= authored
    colors[:, 3] = 1
    return np.clip(colors, 0, 1)


def torso_vest_color(colors, axial_fraction):
    """Recover this prototype character's white vest away from neck and waist.

    The six prism corners lie at the torso extremes, where nearest-vertex
    samples favor exposed skin and trousers. Use the brighter, neutral samples
    in the middle half instead; their median retains the authored off-white.
    Input and output colors are linear glTF vertex colors.
    """
    rgb = colors[:, :3]
    srgb = np.where(rgb <= .0031308, rgb * 12.92, 1.055 * rgb ** (1 / 2.4) - .055)
    middle = (axial_fraction >= .25) & (axial_fraction <= .75)
    neutral = middle & (np.ptp(srgb, axis=1) <= .12)
    if np.count_nonzero(neutral) < 4:
        raise ValueError('Not enough neutral mid-torso samples to fit the white vest')
    brightness = srgb.mean(axis=1)
    cutoff = float(np.quantile(brightness[neutral], .5))
    selected = neutral & (brightness >= cutoff)
    color = np.median(colors[selected], axis=0)
    return color, {
        'policy': 'White vest: median of the brighter half of neutral mid-torso source texture samples',
        'axial_fraction_range': [.25, .75],
        'maximum_srgb_channel_spread': .12,
        'neutral_sample_count': int(np.count_nonzero(neutral)),
        'selected_sample_count': int(np.count_nonzero(selected)),
        'linear_rgba': color.tolist(),
        'srgb_rgb': np.where(color[:3] <= .0031308, color[:3] * 12.92,
                             1.055 * color[:3] ** (1 / 2.4) - .055).tolist(),
    }


def validate_prisms(positions, indices):
    if positions.shape != (60, 3) or indices.shape != (180,):
        raise ValueError('Expected ten six-vertex, six-triangle open prisms')
    edges = Counter()
    adjacency = [set() for _ in positions]
    for triangle in indices.reshape(-1, 3):
        a, b, c = positions[triangle]
        if np.linalg.norm(np.cross(b - a, c - a)) < 1e-7:
            raise ValueError('Degenerate prism triangle')
        for i, j in zip(triangle, np.roll(triangle, -1)):
            i, j = int(i), int(j)
            edges[tuple(sorted((i, j)))] += 1
            adjacency[i].add(j)
            adjacency[j].add(i)
        part = int(triangle[0]) // 6
        if any(int(i) // 6 != part for i in triangle):
            raise ValueError('Prism parts must remain separate components')
        if len({(int(i) % 6) // 3 for i in triangle}) != 2:
            raise ValueError('A top or bottom cap was generated')
        axis = positions[part * 6 + 3:part * 6 + 6].mean(0) - positions[part * 6:part * 6 + 3].mean(0)
        center = positions[part * 6:part * 6 + 6].mean(0)
        normal = np.cross(b - a, c - a)
        radial = (a + b + c) / 3 - center
        radial -= unit(axis) * np.dot(radial, unit(axis))
        if np.dot(normal, radial) <= 0:
            raise ValueError('Prism side winding is not outward')
    remaining = set(range(len(positions)))
    components = 0
    while remaining:
        pending = [remaining.pop()]
        components += 1
        while pending:
            for neighbor in adjacency[pending.pop()]:
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    pending.append(neighbor)
    boundary = sum(value == 1 for value in edges.values())
    if components != 10 or boundary != 60 or any(value > 2 for value in edges.values()):
        raise ValueError('Invalid open-prism topology')
    return {'components': components, 'boundary_edges': boundary, 'triangles': 60,
            'vertices': 60, 'caps': 0, 'outward_winding': True}


def slim_prism_positions(positions, parts, joint_locations):
    """Narrow the torso and terminal hand/foot rings in mesh bind space.

    The torso's fitted ring centers and longitudinal extent stay fixed. Distal
    limb corners move toward the wrist/ankle, reducing both terminal width and
    overhang. A two-ring column consequently tapers more toward the smaller tip.
    Ring metadata in the caller-owned parts is updated to match the result.
    """
    positions = positions.copy()
    changes = []
    for part in parts:
        first = part['first_vertex']
        if part['name'] == 'torso':
            axis = np.asarray(part['axis'])
            for ring_index, ring in enumerate(part['rings']):
                section = slice(first + ring_index * 3, first + ring_index * 3 + 3)
                center = np.asarray(ring['center'])
                offset = positions[section] - center
                longitudinal = np.outer(offset @ axis, axis)
                positions[section] = center + longitudinal + .85 * (offset - longitudinal)
                ring['half_width'] *= .85
                ring['half_depth'] *= .85
            changes.append({'part': part['name'], 'rings': [0, 1],
                            'transverse_scale': .85, 'longitudinal_scale': 1,
                            'anchor': 'each standard fitted ring center'})
        elif part['name'].endswith(('_lower_arm', '_lower_leg')):
            anchor = joint_locations[part['end_joint']]
            section = slice(first + 3, first + 6)
            positions[section] = anchor + .7 * (positions[section] - anchor)
            ring = part['rings'][1]
            ring['center'] = (anchor + .7 * (np.asarray(ring['center']) - anchor)).tolist()
            ring['half_width'] *= .7
            ring['half_depth'] *= .7
            changes.append({'part': part['name'], 'rings': [1], 'scale': .7,
                            'anchor_joint': part['end_joint'], 'anchor': anchor.tolist()})
    return positions, changes


def slim2_torso_positions(positions, parts):
    """Bring the upper rear pair closer and lower it; leave the front fixed.

    The source faces +Z, so the rear upper corners are the two vertices on the
    -Z edge. Height is the pre-change fitted-ring separation along the torso
    axis, in source mesh bind units. The upper rim becomes tilted.
    """
    positions = positions.copy()
    torso = next(part for part in parts if part['name'] == 'torso')
    first = torso['first_vertex']
    rear_indices = [first + 4, first + 5]
    front_index = first + 3
    axis = np.asarray(torso['axis'])
    lower, upper = (np.asarray(ring['center']) for ring in torso['rings'])
    height = float(np.dot(upper - lower, axis))
    midpoint = positions[rear_indices].mean(axis=0)
    shift = -.10 * height * axis
    positions[rear_indices] = midpoint + .85 * (positions[rear_indices] - midpoint) + shift
    ring = torso['rings'][1]
    ring['half_width'] *= .85
    ring['center_semantics'] = 'Pre-slim2 fitted cross-section reference; not the tilted rim centroid'
    ring['vertex_centroid'] = positions[first + 3:first + 6].mean(axis=0).tolist()
    ring['shape'] = 'Tilted triangular rim: rear edge lower than unchanged front apex'
    return positions, {'part': 'torso', 'rings': [1], 'vertex_indices': rear_indices,
                       'unchanged_front_vertex': front_index, 'rear_pair_separation_scale': .85,
                       'rear_pair_midpoint_before': midpoint.tolist(),
                       'rear_pair_midpoint_after': (midpoint + shift).tolist(),
                       'pre_change_axial_height': height, 'axial_lowering_fraction': .10,
                       'translation': shift.tolist(), 'axis': axis.tolist()}


def generate_prism_lod(source, *, color_mode='white_vest', profile=None, slim=False):
    """Build a shape profile while keeping colors fit to standard corners.

    ``sampled`` retains the first experiment's nearest-source coloring;
    ``white_vest`` recovers the authored torso vest. Slim changes geometry only,
    after either color policy has been evaluated on the standard corners.
    Legacy ``slim=True`` remains equivalent to ``profile='slim'``.
    """
    if profile is None:
        profile = 'slim' if slim else 'standard'
    elif slim:
        raise ValueError('Choose profile or the legacy slim=True argument, not both')
    if profile not in ('standard', 'slim', 'slim2'):
        raise ValueError('Unknown prism shape profile: ' + str(profile))
    if color_mode not in ('sampled', 'white_vest'):
        raise ValueError('Unknown prism color mode: ' + str(color_mode))
    document, original_binary = read_glb(source)
    document = copy.deepcopy(document)
    if len(document['meshes']) != 1 or len(document['meshes'][0]['primitives']) != 1 or len(document['skins']) != 1:
        raise ValueError('Prism LODs require one mesh primitive and one skin')
    primitive = document['meshes'][0]['primitives'][0]
    attributes = primitive['attributes']
    positions = numeric_attribute(document, original_binary, attributes['POSITION']).astype(np.float64)
    joints = accessor_array(document, original_binary, attributes['JOINTS_0'])
    weights = numeric_attribute(document, original_binary, attributes['WEIGHTS_0'])
    skin = document['skins'][0]
    names = {document['nodes'][node]['name']: i for i, node in enumerate(skin['joints'])}
    inverse_bind = accessor_array(document, original_binary, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    bind = np.linalg.inv(inverse_bind)
    dominant = joints[np.arange(len(joints)), np.argmax(weights, axis=1)]
    colors = source_colors(document, original_binary, primitive)

    def joint(name):
        return names['bip ' + name]

    def location(name):
        return bind[joint(name), :3, 3].astype(np.float64)

    definitions = []
    for side in ('L', 'R'):
        definitions.extend((
            (side + '_upper_arm', side + ' UpperArm', side + ' Forearm', [side + ' UpperArm'], False),
            (side + '_lower_arm', side + ' Forearm', side + ' Hand',
             [side + suffix for suffix in (' Forearm', ' Hand', ' Finger0', ' Finger01', ' Finger1', ' Finger11')], True),
            (side + '_upper_leg', side + ' Thigh', side + ' Calf', [side + ' Thigh'], False),
            (side + '_lower_leg', side + ' Calf', side + ' Foot',
             [side + ' Calf', side + ' Foot', side + ' Toe0'], True),
        ))
    definitions.extend((
        ('torso', 'Pelvis', 'Neck', ['Pelvis', 'Spine', 'Spine1', 'Neck', 'L Clavicle', 'R Clavicle'], True),
        ('head', 'Head', None, ['Head', 'jaw'], True),
    ))
    output_positions, output_colors, output_joints, output_weights, output_indices, parts = [], [], [], [], [], []
    torso_color_metadata = None
    for name, start_joint, end_joint, group_names, fit_extent in definitions:
        # Some skins omit unused terminal bones (Female D's right fingers).
        # Segment start/end joints remain required for fitting and attachment.
        group = np.asarray([joint(n) for n in group_names if 'bip ' + n in names])
        selected_indices = np.flatnonzero(np.isin(dominant, group))
        points = positions[selected_indices]
        if len(points) < 3:
            raise ValueError('Not enough skin vertices to fit prism part: ' + name)
        start = location(start_joint)
        if end_joint:
            end = location(end_joint)
        else:
            # This rig's head local +X is the cranial direction, in bind mesh units.
            end = start + unit(bind[joint(start_joint), :3, 0]) * 20
        axis = unit(end - start)
        length = np.linalg.norm(end - start)
        preferred = np.array([1., 0, 0]) if abs(axis[0]) < .8 else np.array([0., 1, 0])
        u = unit(preferred - axis * np.dot(preferred, axis))
        v = unit(np.cross(axis, u))
        if np.dot(v, [0, 0, 1]) < 0:
            u, v = -u, -v
        axial = (points - start) @ axis
        if name == 'torso' and color_mode == 'white_vest':
            vest_color, torso_color_metadata = torso_vest_color(colors[selected_indices], axial / length)
        low, high = (-.04 * length, 1.04 * length)
        if fit_extent:
            low = min(low, float(np.quantile(axial, .01)))
            high = max(high, float(np.quantile(axial, .99)))
        if name == 'head':
            low, high = float(np.min(axial)) - .5, float(np.max(axial)) + .5
        # The single torso includes the pelvis and reaches the neck, while its
        # two rings follow the pelvis and chest instead of bending a rigid trunk.
        ring_joints = (joint(start_joint), joint('Spine1') if name == 'torso' else joint(start_joint))
        rings = []
        ring_metadata = []
        for ring, distance in enumerate((low, high)):
            near = np.abs(axial - distance) < (high - low) * .30
            if np.count_nonzero(near) < 8:
                near = np.ones(len(points), dtype=bool)
            cross = np.column_stack(((points - start) @ u, (points - start) @ v))
            bounds = np.quantile(cross[near], [.03, .97], axis=0)
            center = bounds.mean(0)
            radius = np.maximum((bounds[1] - bounds[0]) * .5, .75)
            # Three corners approximate the fitted cross-section. Slight width
            # inflation protects thin limbs while retaining exactly three faces.
            cross_triangle = np.array([[0, 1], [-1.08, -1], [1.08, -1]]) * radius + center
            ring_points = start + axis * distance + cross_triangle[:, :1] * u + cross_triangle[:, 1:] * v
            rings.extend(ring_points)
            ring_metadata.append({'center': (start + axis * distance + u * center[0] + v * center[1]).tolist(),
                                  'half_width': float(radius[0]), 'half_depth': float(radius[1]),
                                  'joint': document['nodes'][skin['joints'][ring_joints[ring]]]['name']})
        vertices = np.asarray(rings)
        base = len(output_positions)
        local_indices = []
        for i in range(3):
            j = (i + 1) % 3
            local_indices.extend((i, j, j + 3, i, j + 3, i + 3))
        # Preserve same-part nearest samples for the limbs and head. The torso
        # uses its vest color because its end rings sit at the neck and trousers.
        for i, vertex in enumerate(vertices):
            nearest = np.argsort(np.sum((points - vertex) ** 2, axis=1))[:3]
            output_colors.append(vest_color if name == 'torso' and color_mode == 'white_vest' else
                                 np.mean(colors[selected_indices[nearest]], axis=0))
            output_positions.append(vertex)
            output_joints.append([ring_joints[i // 3], 0, 0, 0])
            output_weights.append([1., 0, 0, 0])
        output_indices.extend(base + i for i in local_indices)
        parts.append({'name': name, 'source_vertex_count': len(points), 'start_joint': start_joint,
                      'end_joint': end_joint, 'axis': axis.tolist(), 'rings': ring_metadata,
                      'first_vertex': base, 'vertex_count': 6, 'triangle_count': 6,
                      'includes_terminal_extent': fit_extent and name not in ('torso', 'head')})
    fitted_positions = np.asarray(output_positions, dtype=np.float64)
    shape_changes = []
    if profile in ('slim', 'slim2'):
        joint_locations = {part['end_joint']: location(part['end_joint']) for part in parts
                           if part['name'].endswith(('_lower_arm', '_lower_leg'))}
        fitted_positions, shape_changes = slim_prism_positions(fitted_positions, parts, joint_locations)
    if profile == 'slim2':
        fitted_positions, torso_change = slim2_torso_positions(fitted_positions, parts)
        shape_changes.append(torso_change)
    out_positions = fitted_positions.astype('<f4')
    out_indices = np.asarray(output_indices, dtype='<u4')
    topology = validate_prisms(out_positions.astype(np.float64), out_indices)
    normals = np.zeros_like(out_positions, dtype=np.float64)
    for triangle in out_indices.reshape(-1, 3):
        a, b, c = out_positions[triangle].astype(np.float64)
        normal = unit(np.cross(b - a, c - a))
        for vertex in triangle:
            normals[vertex] += normal
    normals /= np.linalg.norm(normals, axis=1)[:, None]
    binary = bytearray(original_binary)

    def append(array, kind, component, target):
        array = np.asarray(array)
        binary.extend(b'\0' * (-len(binary) % 4))
        view = len(document['bufferViews'])
        document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary),
                                      'byteLength': array.nbytes, 'target': target})
        binary.extend(array.tobytes())
        accessor = {'bufferView': view, 'componentType': component, 'count': len(array), 'type': kind}
        if kind == 'VEC3' and array.dtype.kind == 'f':
            accessor.update(min=array.min(0).tolist(), max=array.max(0).tolist())
        document['accessors'].append(accessor)
        return len(document['accessors']) - 1

    primitive['attributes'] = {
        'POSITION': append(out_positions, 'VEC3', 5126, 34962),
        'NORMAL': append(normals.astype('<f4'), 'VEC3', 5126, 34962),
        'COLOR_0': append(np.asarray(output_colors, dtype='<f4'), 'VEC4', 5126, 34962),
        'JOINTS_0': append(np.asarray(output_joints, dtype='<u2'), 'VEC4', 5123, 34962),
        'WEIGHTS_0': append(np.asarray(output_weights, dtype='<f4'), 'VEC4', 5126, 34962),
    }
    primitive['indices'] = append(out_indices[:, None], 'SCALAR', 5125, 34963)
    primitive['material'] = len(document['materials'])
    document['materials'].append({'name': 'open_prism_baked_colors', 'doubleSided': False,
        'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0, 'roughnessFactor': 1}})
    document['buffers'][0]['byteLength'] = len(binary)
    document.setdefault('extras', {})['zombie_lod'] = {'version': 1,
        'source_sha256': hashlib.sha256(source).hexdigest(), 'method': 'open_triangular_prisms'}
    result = pack_glb(document, bytes(binary))
    output_document, output_binary = read_glb(result)
    original_document, _ = read_glb(source)
    for key in ('nodes', 'scenes', 'scene', 'skins', 'animations'):
        if output_document.get(key) != original_document.get(key):
            raise ValueError('Prism generation changed original rig/animation: ' + key)
    if output_binary[:len(original_binary)] != original_binary:
        raise ValueError('Prism generation changed original binary data')
    return result, {'version': 1, 'method': 'open_triangular_prisms',
        'profile': profile, 'color_mode': color_mode,
        'shape_changes': shape_changes,
        'units': 'source mesh bind units; original scene scale is retained',
        'source_sha256': hashlib.sha256(source).hexdigest(), 'sha256': hashlib.sha256(result).hexdigest(),
        'topology': topology, 'parts': parts,
        'cross_section': {'fit': '3rd/97th percentile source-vertex bounds in each end region',
                          'corners_in_half_extent_units': [[0, 1], [-1.08, -1], [1.08, -1]],
                          'orientation': 'single vertex toward source bind +Z; opposite edge spans full width',
                          'tradeoff': 'Frontal/side extents approximately retained; oblique widths vary with triangular cross-section'},
        'appearance': ('Source white-vest median on torso; three nearest same-part source texture samples per limb/head corner; linear vertex colors'
                       if color_mode == 'white_vest' else
                       'Three nearest same-part source texture samples per standard corner; linear vertex colors'),
        'torso_color': torso_color_metadata,
        'limitations': ['No top or bottom caps', 'No separate hands or feet; lower columns include approximate terminal extents',
                       'Rigid limb/head attachment; torso rings follow pelvis and chest', 'Single-sided exterior faces']}
