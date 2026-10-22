#!/usr/bin/env python3
"""Bake sparse structural hierarchy and certified single-mouth pocket geometry.

Coordinates are the replay's X east / Z south meters. No raster, path, sampled
potential field, or per-request geometry is produced. Closed structural OBBs use
the original PSS padding; trees affect only the final pocket exit endpoint check.
"""
import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
import shapely
from shapely.geometry.polygon import orient

from prepare_potential_surface_requests import PhysicalGeometry


POCKET_PARAMETERS = {
    'minimum_depth_m': 3.0,
    'minimum_depth_over_width': .5,
    'minimum_width_m': .604,
    'maximum_width_m': 128.0,
    'exit_target_distance_m': 2.0,
    'exit_release_outset_m': 1.0,
    'pocket_avoidance_support_m': 12.0,
    'pocket_avoidance_length_m': 2.0,
    'pocket_avoidance_weight': 1.0,
}
BUMP_SCALES = (
    {'level': 0, 'minimum_span_m': 16.0, 'maximum_span_m': 64.0,
     'support_outset_m': 4.0, 'weight': .75},
    {'level': 1, 'minimum_span_m': 64.0, 'maximum_span_m': 192.0,
     'support_outset_m': 8.0, 'weight': 1.5},
)


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def polygon_parts(geometry):
    """Do not reinterpret line/point contacts as positive-area free regions."""
    if geometry.geom_type == 'Polygon':
        return [geometry] if geometry.area > 0 else []
    if geometry.geom_type in ('MultiPolygon', 'GeometryCollection'):
        return [part for child in geometry.geoms for part in polygon_parts(child)]
    return []


def line_parts(geometry):
    if geometry.geom_type in ('LineString', 'LinearRing'):
        return [geometry] if geometry.length > 0 else []
    if geometry.geom_type in ('MultiLineString', 'GeometryCollection'):
        return [part for child in geometry.geoms for part in line_parts(child)]
    return []


def structural_geometry(document):
    physical = PhysicalGeometry(document)
    selected = np.flatnonzero(physical.kinds != 1)
    centers, axes = physical.centers[selected], physical.axes[selected]
    second, halves = physical.perpendiculars[selected], physical.halves[selected]
    corners = np.stack([centers + sx*halves[:, :1]*axes + sz*halves[:, 1:]*second
                        for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))], axis=1)
    rectangles = shapely.polygons(corners)
    if not np.isfinite(corners).all() or not shapely.is_valid(rectangles).all():
        raise ValueError('Invalid original padded structural OBB geometry')
    blocked = shapely.union_all(rectangles)
    domain = shapely.box(*physical.minimum, *physical.maximum)
    if not shapely.is_valid(blocked) or not shapely.is_valid(domain):
        raise ValueError('Invalid structural union or inset domain')
    return physical, selected, rectangles, blocked, domain


@dataclass
class Node:
    box: np.ndarray
    begin: int
    end: int
    children: tuple = ()


def structural_bvh(rectangles):
    """Stable median splits over original physical AABBs, one obstacle per leaf."""
    boxes = np.asarray(shapely.bounds(rectangles), dtype=np.float64).reshape((-1, 4))
    centers = (boxes[:, :2]+boxes[:, 2:])*.5
    order = np.arange(len(boxes), dtype=np.int64)
    nodes = []

    def build(begin, end):
        indices = order[begin:end]
        box = np.r_[boxes[indices, :2].min(axis=0), boxes[indices, 2:].max(axis=0)]
        index = len(nodes)
        nodes.append(Node(box, begin, end))
        if end-begin > 1:
            spans = np.ptp(centers[indices], axis=0)
            axis = int(spans[1] > spans[0])
            # Stable order gives deterministic source-index ties at every split.
            order[begin:end] = indices[np.argsort(centers[indices, axis], kind='stable')]
            middle = (begin+end)//2
            nodes[index].children = (build(begin, middle), build(middle, end))
        return index

    if len(boxes):
        build(0, len(boxes))
    return nodes, order


def frontier(nodes, minimum_span, maximum_span):
    """One ancestor-free frontier per scale; raw AABBs may overlap spatially."""
    result = []

    def visit(index):
        node = nodes[index]
        span = float(np.max(node.box[2:]-node.box[:2]))
        if index and node.children and minimum_span <= span <= maximum_span:
            result.append(index)
        elif node.children and span >= minimum_span:
            for child in node.children:
                visit(child)

    if nodes:
        visit(0)
    return result


def make_bumps(rectangles):
    nodes, _ = structural_bvh(rectangles)
    result, counts = [], {}
    for scale in BUMP_SCALES:
        chosen = frontier(nodes, scale['minimum_span_m'], scale['maximum_span_m'])
        counts[str(scale['level'])] = len(chosen)
        for index in chosen:
            node = nodes[index]
            low, high = node.box[:2], node.box[2:]
            radii = math.sqrt(2)*((high-low)*.5+scale['support_outset_m'])
            result.append({'center': ((low+high)*.5).tolist(), 'radii': radii.tolist(),
                           'weight': scale['weight'], 'box': node.box.tolist(),
                           'level': scale['level'], 'obstacle_count': node.end-node.begin,
                           'node_id': index})
    return result, {'bvh_nodes': len(nodes), 'bumps_by_level': counts,
                    'bumps': len(result)}


def structural_segment_clear(physical, start, end, open_ends=False):
    """Original normalized local-axis slabs, independent of GEOS corner unions.

    A mouth may touch barriers exactly at its endpoints. All interior contacts
    reject it, including an isolated contact or a numerical hull-cap sliver.
    """
    start, end = np.asarray(start), np.asarray(end)
    indices = physical.tree.query(shapely.LineString((start, end)))
    delta = end-start
    for index in indices:
        if physical.kinds[index] == 1:
            continue
        offset = start-physical.centers[index]
        first, last = 0., 1.
        hit = True
        for axis, half in zip((physical.axes[index], physical.perpendiculars[index]), physical.halves[index]):
            position = float(offset[0]*axis[0]+offset[1]*axis[1])
            velocity = float(delta[0]*axis[0]+delta[1]*axis[1])
            if velocity == 0:
                if position < -half or position > half:
                    hit = False
                    break
                continue
            a, b = (-half-position)/velocity, (half-position)/velocity
            first, last = max(first, min(a, b)), min(last, max(a, b))
            if first > last:
                hit = False
                break
        if hit and (not open_ends or (last > 0 and first < 1)):
            return False
    return True


def certify_pocket(candidate, hull, blocked, domain, physical, rejected):
    """Require one exact straight hull opening and otherwise original barriers.

    Certification uses GEOS set operations without a buffer, simplification,
    coordinate grid or geometric epsilon. Numerical uncertainty rejects a bay.
    Original tree-circle tests independently reject a blocked exit endpoint.
    """
    if not candidate.is_valid or not candidate.area > 0:
        rejected['invalid_candidate'] += 1
        return None
    opening = shapely.line_merge(candidate.boundary.intersection(hull.boundary))
    pieces = line_parts(opening)
    if len(pieces) != 1:
        rejected['closed_or_multiple_mouths'] += 1
        return None
    coordinates = list(pieces[0].coords)
    mouth = shapely.LineString((coordinates[0], coordinates[-1]))
    if (mouth.length <= 0 or not mouth.covers(pieces[0]) or
            not pieces[0].covers(mouth)):
        rejected['nonstraight_mouth'] += 1
        return None
    width = mouth.length
    if not POCKET_PARAMETERS['minimum_width_m'] <= width <= POCKET_PARAMETERS['maximum_width_m']:
        rejected['mouth_width'] += 1
        return None
    endpoints = shapely.MultiPoint((coordinates[0], coordinates[-1]))
    # A touching blocker in the middle would split the opening physically even
    # if line_merge joined its two collinear pieces at that closed contact.
    if not mouth.intersection(blocked).difference(endpoints).is_empty:
        rejected['mouth_blocked'] += 1
        return None
    if not structural_segment_clear(physical, coordinates[0], coordinates[-1], open_ends=True):
        rejected['mouth_native_slab_contact'] += 1
        return None
    walls = candidate.boundary.difference(mouth)
    if not walls.difference(blocked.boundary).is_empty:
        rejected['artificial_or_domain_seam'] += 1
        return None
    first, last = np.asarray(mouth.coords[0]), np.asarray(mouth.coords[-1])
    middle = (first+last)*.5
    along = (last-first)/width
    outward = np.array([-along[1], along[0]])
    inside = np.asarray(hull.representative_point().coords[0])
    if np.dot(outward, inside-middle) > 0:
        outward = -outward
    signed = (np.asarray(candidate.exterior.coords)-middle)@outward
    depth = -float(signed.min())
    if depth < POCKET_PARAMETERS['minimum_depth_m'] or depth/width < POCKET_PARAMETERS['minimum_depth_over_width']:
        rejected['insufficient_depth'] += 1
        return None
    target = middle+outward*POCKET_PARAMETERS['exit_target_distance_m']
    exit_segment = shapely.LineString((middle, target))
    if (not domain.contains(exit_segment) or blocked.intersects(exit_segment) or
            not structural_segment_clear(physical, middle, target)):
        rejected['structural_exit_blocked'] += 1
        return None
    if not physical.clear(target):
        rejected['physical_exit_endpoint_blocked'] += 1
        return None
    if candidate.intersection(blocked).area != 0 or not domain.covers(candidate):
        rejected['nonfree_candidate'] += 1
        return None
    candidate = orient(candidate, sign=1)
    result = {'outer': [list(point) for point in list(candidate.exterior.coords)[:-1]],
              'holes': [[list(point) for point in list(ring.coords)[:-1]]
                        for ring in candidate.interiors],
              'mouth': [first.tolist(), last.tolist()], 'outward': outward.tolist(),
              'depth': depth, 'width': width}
    return candidate, result


def make_pockets(blocked, domain, physical):
    parts = polygon_parts(blocked)
    obstacle_index = shapely.STRtree(parts)
    accepted, rejected = [], Counter()
    concave_parts = free_candidates = 0
    for part in parts:
        hull = part.convex_hull
        bays = hull.difference(part)
        if bays.is_empty:
            continue
        concave_parts += 1
        # This AABB includes every bay and every possible 2 m exit segment.
        # Its index filters work only; it does not alter obstacle geometry.
        outset = POCKET_PARAMETERS['exit_target_distance_m']
        extent = np.asarray(hull.bounds)+outset*np.array([-1., -1., 1., 1.])
        nearby = obstacle_index.query(shapely.box(*extent))
        local_blocked = shapely.union_all([parts[int(i)] for i in nearby])
        for candidate in polygon_parts(bays.difference(local_blocked).intersection(domain)):
            free_candidates += 1
            result = certify_pocket(candidate, hull, local_blocked, domain, physical, rejected)
            if result is not None:
                accepted.append(result)
    # Prefer the smaller, more specific certified pocket when independent
    # obstacle hulls overlap. Retained interiors are strictly disjoint.
    accepted.sort(key=lambda pair: (pair[0].area, pair[0].bounds, pair[0].wkb))
    kept = []
    if accepted:
        index = shapely.STRtree([pair[0] for pair in accepted])
        kept_ids = set()
        for i, (polygon, record) in enumerate(accepted):
            if any(int(j) in kept_ids and polygon.intersection(accepted[int(j)][0]).area > 0
                   for j in index.query(polygon)):
                rejected['overlapping_certified_pocket'] += 1
                continue
            kept_ids.add(i)
            kept.append(record)
    return kept, {'structural_union_polygons': len(parts),
                  'concave_structural_polygons': concave_parts,
                  'free_bay_candidates': free_candidates,
                  'certified_before_overlap_filter': len(accepted),
                  'pockets': len(kept), 'pocket_rejections': dict(sorted(rejected.items()))}


def bake(scene, output):
    scene, output = Path(scene), Path(output)
    if output.exists():
        raise ValueError('Output exists; preserve it and choose a new filename')
    started = time.perf_counter()
    raw = scene.read_bytes()
    document = json.loads(raw)
    physical, selected, rectangles, blocked, domain = structural_geometry(document)
    bumps, bump_stats = make_bumps(rectangles)
    pockets, pocket_stats = make_pockets(blocked, domain, physical)
    if sha256(scene.read_bytes()) != sha256(raw):
        raise RuntimeError('Source scene changed during baking')
    result = {'schema': 1, 'source': str(scene.resolve()), 'source_sha256': sha256(raw),
              'script_sha256': sha256(Path(__file__).read_bytes()),
              'physical_geometry_module_sha256': sha256(Path(__file__).with_name('prepare_potential_surface_requests.py').read_bytes()),
              'physical_padding': {'radius_m': physical.radius, 'skin_m': physical.skin,
                                   'building_extra_m': physical.building_extra},
              'minimum': physical.minimum.tolist(), 'maximum': physical.maximum.tolist(),
              'parameters': {
                  'pockets': dict(POCKET_PARAMETERS), 'bumps': list(BUMP_SCALES),
                  'bvh': 'Stable median split along greatest centroid spread; X breaks ties; one original padded structural OBB per leaf. No root, at least two obstacles per selected internal node. Each scale is an ancestor-free frontier; boxes may overlap spatially.',
                  'bump_ellipse': 'radii = sqrt(2) * (raw physical AABB half-extents + scale support_outset_m)',
                  'bump_weight_normalization': 'Raw JSON weight is divided by the number of distinct present levels exactly once at runtime. Goal inside the closed raw box suppresses that bump.',
                  'pocket_model': 'Convex hull minus each connected building/fence union polygon, then subtract every structural obstacle and clip to strict center-inset domain. One exact straight hull mouth; every other boundary on the original structural union. Closed holes, multiple mouths, artificial/domain seams and middle-mouth obstacle contacts rejected. Mouth interior and outward exit segment also checked with original normalized local-axis closed slab tests. Retained pocket interiors do not overlap.',
                  'exit_policy': 'Target is mouth midpoint plus outward * 2 m. Entire outward segment must be structurally free and within inset domain. Target endpoint additionally passes independent original tree-circle/OBB physical checks. Trees do not shape pockets or hierarchy. Runtime rechecks target and uses 1 m outward release hysteresis.',
                  'geometry_tolerance_m': 0.0,
                  'stored_representation': 'Sparse analytic geometry only; no raster, graph, sampled potential, path or request data.'},
              'diagnostics': {'buildings': int(np.sum(physical.kinds[selected] == 0)),
                              'fences': int(np.sum(physical.kinds[selected] == 2)),
                              'ignored_tree_shapes': int(np.sum(physical.kinds == 1)),
                              **bump_stats, **pocket_stats,
                              'pocket_outer_vertices': sum(len(p['outer']) for p in pockets),
                              'pocket_hole_vertices': sum(len(r) for p in pockets for r in p['holes']),
                              'elapsed_seconds': time.perf_counter()-started},
              'numpy_version': np.__version__, 'shapely_version': shapely.__version__,
              'geos_version': shapely.geos_version_string,
              'floating_precision_caveat': 'GEOS double polygon corners can differ by arithmetic ulps from native local-axis slabs. Certification adds no geometric epsilon, margin, buffer or snapping. It rejects uncertain/artificial boundaries; runtime physical guards remain authoritative.',
              'bumps': bumps, 'pockets': pockets}
    encoded = (json.dumps(result, indent=2, allow_nan=False)+'\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name+'.tmp')
    with temporary.open('xb') as stream:
        stream.write(encoded)
    if output.exists():
        raise ValueError('Output appeared during baking; temporary evidence preserved')
    temporary.replace(output)
    print(json.dumps({'output': str(output), 'sha256': sha256(encoded), 'bytes': len(encoded),
                      **result['diagnostics']}, sort_keys=True), flush=True)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Fresh JSON filename')
    arguments = parser.parse_args()
    bake(arguments.scene, arguments.output)
