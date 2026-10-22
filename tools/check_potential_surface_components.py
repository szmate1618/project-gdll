#!/usr/bin/env python3
"""Exact declared PSS building/fence free-space interiors, with trees omitted."""
import argparse
import collections
import hashlib
import json
import time
from pathlib import Path

import numpy as np
import shapely

ROOT = Path(__file__).resolve().parents[1]
from prepare_potential_surface_requests import PhysicalGeometry
VARIANTS = ('circles-filtered', 'rectangles-filtered', 'rectangles-normalized', 'rectangles-tangent')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def locate(point, components, index):
    if len(point) != 2 or not np.isfinite(point).all():
        raise ValueError('Endpoint must contain two finite X/Z coordinates')
    value = shapely.Point(*point)
    candidates = index.query(value)
    hits = [int(i) for i in candidates if shapely.contains(components[i], value)]
    if len(hits) > 1:
        raise RuntimeError('Free-space component interiors overlap')
    return hits[0] if hits else None


def run(run_directory, output_directory):
    """Classify the original endpoints without trees or conservative bake margins."""
    started = time.perf_counter()
    run_directory, output_directory = run_directory.resolve(), output_directory.resolve()
    outputs = ('physical-rectangles-connectivity.json', 'physical-rectangles-free-space.wkb')
    if any((output_directory / name).exists() for name in outputs):
        raise ValueError('Choose a fresh output directory; preserve existing connectivity evidence')
    output_directory.mkdir(parents=True, exist_ok=True)
    paths = {(cohort, variant): run_directory / f'{cohort}-{variant}' / 'simulation/summary.json'
             for cohort in ('training', 'second-seed') for variant in VARIANTS}
    documents = {key: json.loads(path.read_text()) for key, path in paths.items()}
    source = Path(documents['training', 'rectangles-tangent']['source'])
    source_hash = sha256(source)
    if any(d['source_sha256'] != source_hash for d in documents.values()):
        raise ValueError('A summary was produced from different source scene bytes')
    for document in documents.values():
        config = document['configuration']
        if (config['radius_m'], config['skin_m'], config['building_extra_m']) != (.3, .002, .05):
            raise ValueError('Physical configuration differs from the declared .302/.352 m model')
    geometry = PhysicalGeometry(json.loads(source.read_text()))
    selected = np.flatnonzero(geometry.kinds != 1)
    centers, axes = geometry.centers[selected], geometry.axes[selected]
    second, halves = geometry.perpendiculars[selected], geometry.halves[selected]
    coordinates = np.stack([centers + sx * halves[:, :1] * axes + sz * halves[:, 1:] * second
                            for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))], axis=1)
    if not np.isfinite(coordinates).all():
        raise RuntimeError('Nonfinite physical OBB coordinates')
    blocked = shapely.polygons(coordinates)
    if not shapely.is_valid(blocked).all() or not np.all(shapely.area(blocked) > 0):
        raise RuntimeError('Invalid padded physical OBB')
    domain = shapely.box(*geometry.minimum, *geometry.maximum)
    union = shapely.union_all(blocked)
    clipped_blocked = shapely.intersection(domain, union)
    free = shapely.difference(domain, union)
    if not all(shapely.is_valid(value) for value in (domain, union, clipped_blocked, free)):
        raise RuntimeError('Invalid union or physical free-space geometry')
    parts = shapely.get_parts(free)
    components = np.asarray([p for p in parts if p.geom_type == 'Polygon' and p.area > 0], dtype=object)
    if not len(components) or any(p.geom_type not in ('Polygon', 'MultiPolygon') for p in parts):
        raise RuntimeError('Unexpected free-space geometry')
    if any(not shapely.covered_by(component, domain) for component in components):
        raise RuntimeError('Component leaves inset map domain')
    components = components[np.argsort(-shapely.area(components), kind='stable')]
    index = shapely.STRtree(components)
    map_area, blocked_area, free_area = map(float, shapely.area([domain, clipped_blocked, free]))
    area_error = abs(map_area-blocked_area-free_area)
    if area_error > map_area*1e-10 or abs(float(shapely.area(components).sum())-free_area) > map_area*1e-10:
        raise RuntimeError('Physical free-space area coverage is inconsistent')
    wkb = output_directory / 'physical-rectangles-free-space.wkb'
    wkb.write_bytes(shapely.to_wkb(free))
    report = {
        'schema': 1, 'source': str(source), 'source_sha256': source_hash,
        'script_sha256': sha256(Path(__file__)),
        'physical_geometry_module_sha256': sha256(ROOT / 'tools/prepare_potential_surface_requests.py'),
        'model': 'Original normalized building/fence OBBs padded per axis by 0.300+0.002 m, '
                 'plus 0.050 m for buildings. Trees intentionally omitted. NO 0.005 m numerical margin. '
                 'Domain is strict interior of the raster rectangle inset by 0.302 m. '
                 'Each polygon interior is one connected component; zero-width boundary contacts do not connect components. '
                 'Endpoints use strict contains; no snapping, epsilon, tolerance or coordinate quantization.',
        'interpretation': 'Different component IDs certify a building/fence obstruction in this declared '
                          'double-precision physical model even with all trees removed. Matching IDs only '
                          'establish tree-free connectivity; trees may obstruct the real route. Unlocated '
                          'endpoints remain unclassified, not counted as disconnected.',
        'floating_precision_caveat': 'GEOS operates on double-precision polygon vertices computed from '
                                     'normalized OBB axes. Its corner-coordinate construction can differ by '
                                     'a few arithmetic ulps from the native local-axis slab test. Exact tangencies '
                                     'and extremely narrow gaps remain sensitive to floating arithmetic; no geometric '
                                     'tolerance or margin is added to force a connectivity outcome.',
        'shapely_version': shapely.__version__, 'geos_version': shapely.geos_version_string,
        'numpy_version': np.__version__,
        'geometry': {'buildings': int(np.sum(geometry.kinds[selected] == 0)),
                     'fences': int(np.sum(geometry.kinds[selected] == 2)),
                     'ignored_trees': int(np.sum(geometry.kinds == 1)),
                     'map_inset_minimum': geometry.minimum.tolist(), 'map_inset_maximum': geometry.maximum.tolist(),
                     'domain_area_m2': map_area, 'blocked_area_within_domain_m2': blocked_area,
                     'free_area_m2': free_area, 'area_partition_error_m2': area_error,
                     'components': len(components), 'component_areas_m2': shapely.area(components).tolist(),
                     'valid_input_rectangles': True, 'valid_union_and_free_space': True,
                     'components_within_domain': True,
                     'free_space_wkb': str(wkb), 'free_space_wkb_sha256': sha256(wkb)},
        'inputs': {str(path.relative_to(run_directory)): sha256(path) for path in paths.values()},
        'cohorts': {},
    }
    for cohort in ('training', 'second-seed'):
        reference = documents[cohort, 'rectangles-tangent']
        if any(documents[cohort, v]['requests'] != reference['requests'] for v in VARIANTS):
            raise ValueError('Steering variants contain different endpoint requests')
        agents_by_variant = {v: {a['id']: a for a in documents[cohort, v]['agents']} for v in VARIANTS}
        request_ids = [request['id'] for request in reference['requests']]
        if len(set(request_ids)) != len(request_ids):
            raise ValueError('Duplicate request ID')
        for variant, agents in agents_by_variant.items():
            if len(agents) != len(documents[cohort, variant]['agents']) or set(agents) != set(request_ids):
                raise ValueError('Final agents and requests must have identical unique IDs')
        by_status = {v: {} for v in VARIANTS}
        entries = []
        coverage = collections.Counter()
        for request in reference['requests']:
            start, goal = locate(request['start'], components, index), locate(request['goal'], components, index)
            relation = ('unlocated' if start is None or goal is None else
                        'same_component' if start == goal else 'different_components')
            coverage['start_located'] += start is not None
            coverage['goal_located'] += goal is not None
            coverage[relation] += 1
            statuses = {v: agents_by_variant[v][request['id']]['status'] for v in VARIANTS}
            for variant, status in statuses.items():
                by_status[variant].setdefault(status, collections.Counter())[relation] += 1
            entries.append({'id': request['id'], 'start': request['start'], 'goal': request['goal'],
                            'start_component': start, 'goal_component': goal, 'relation': relation,
                            'statuses': statuses})
        report['cohorts'][cohort] = {'coverage': dict(coverage),
            'by_variant_status': {v: {s: dict(counts) for s, counts in groups.items()} for v, groups in by_status.items()},
            'requests': entries}
        print(cohort, json.dumps(report['cohorts'][cohort]['coverage']), flush=True)
        print('status_relation_counts', json.dumps(report['cohorts'][cohort]['by_variant_status']), flush=True)
    report['arrived_component_calibration'] = {
        cohort: {variant: {
            'arrived': sum(r['statuses'][variant] == 'arrived' for r in group['requests']),
            'different_components': sum(r['statuses'][variant] == 'arrived' and r['relation'] == 'different_components'
                                        for r in group['requests']),
            'unlocated': sum(r['statuses'][variant] == 'arrived' and r['relation'] == 'unlocated'
                            for r in group['requests']),
        } for variant in VARIANTS} for cohort, group in report['cohorts'].items()
    }
    report['arrived_component_calibration_passed'] = all(
        value['different_components'] == value['unlocated'] == 0
        for group in report['arrived_component_calibration'].values() for value in group.values())
    report['elapsed_seconds'] = time.perf_counter()-started
    output = output_directory / 'physical-rectangles-connectivity.json'
    temporary = output.with_suffix(output.suffix + '.tmp')
    temporary.write_text(json.dumps(report, indent=2)+'\n')
    temporary.replace(output)
    print(output, 'components', len(components), 'seconds', report['elapsed_seconds'], flush=True)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-directory', type=Path, required=True,
                        help='Comparison run containing both cohorts and all four steering variants')
    parser.add_argument('--output-directory', type=Path,
                        help='Fresh output directory (default: RUN/reachability)')
    args = parser.parse_args()
    result = run(args.run_directory, args.output_directory or args.run_directory / 'reachability')
    raise SystemExit(0 if result['arrived_component_calibration_passed'] else 2)
