#!/usr/bin/env python3
"""Compare successive shape, normalization and tangent-steering experiments."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

import numpy as np

VARIANTS = (
    ('circles-filtered', False, 'filtered'),
    ('rectangles-filtered', True, 'filtered'),
    ('rectangles-normalized', True, 'normalized'),
    ('rectangles-tangent', True, 'tangent'),
)


def distribution(values):
    return {'count': len(values), 'min': min(values), 'median': statistics.median(values),
            'mean': statistics.mean(values), 'p90': float(np.percentile(values, 90)),
            'p95': float(np.percentile(values, 95)), 'max': max(values)} if values else None


def compare(directory):
    plan = json.loads((directory/'plan.json').read_text())
    parameters = plan['parameters']
    result = {'parameters': parameters, 'timing_scope': plan['timing_scope'], 'scenarios': {}}
    rows = []
    for cohort in ('training', 'second-seed'):
        documents, frames, entries = {}, {}, {}
        for variant, exact, mode in VARIANTS:
            case = directory/f'{cohort}-{variant}'
            document = json.loads((case/'simulation/summary.json').read_text())
            audit = json.loads((case/'audit.json').read_text())
            c, s = document['configuration'], document['summary']
            assert document['native_validation_passed'] and audit['passed']
            assert s['population'] == 1000 and len(document['samples']) == 1
            assert s['statuses']['invalid_endpoint'] == s['invalid_moves'] == s['validation_violations'] == 0
            assert c['exact_buildings'] == exact and c['steering_mode'] == mode
            assert (c['weight'], c['length_scale_m'], c['support_m']) == (
                parameters['weight'], parameters['length_scale'], parameters['support'])
            assert (c['timestep_seconds'], c['speed_m_per_second'], c['radius_m'],
                    c['skin_m'], c['building_extra_m']) == (.05, 2, .3, .002, .05)
            assert not c['navmesh'] and not c['global_paths'] and not c['agent_agent_collisions']
            assert audit['source_sha256'] == document['source_sha256']
            assert audit['segments'] == s['moved_segments']
            with (case/'simulation/steps.csv').open() as stream:
                fixed = [r for r in csv.DictReader(stream) if int(r['step']) < 100 and int(r['active']) == 1000]
            frames[variant] = {(int(r['repeat']), int(r['step'])): r for r in fixed}
            assert len(frames[variant]) == len(fixed)
            stalled = [a for a in document['agents'] if a['status'] == 'stalled']
            timeouts = [a for a in document['agents'] if a['status'] == 'timeout']
            remaining = lambda agents: [math.dist(a['final'], a['goal']) for a in agents]
            entries[variant] = {
                'statuses': s['statuses'], 'updates': s['updates'],
                'whole_trip_cpu_ms': s['cpu_ms'], 'whole_trip_elapsed_ms': s['total_ms'],
                'steering_stats': s['steering_stats'], 'query_stats': s['query_stats'],
                'world_bytes': s['world_bytes'], 'scratch_bytes': s['simulation_scratch_bytes'],
                'clipped_moves': s['clipped_moves'], 'audit': audit,
                'stalled_remaining_m': distribution(remaining(stalled)),
                'timeout_remaining_m': distribution(remaining(timeouts)),
                'whole_cohort_remaining_fraction': distribution([
                    math.dist(a['final'], a['goal'])/a['initial_distance_m'] for a in document['agents']]),
                'arrived_travel_to_direct_ratio': distribution([
                    a['travel_m']/a['initial_distance_m'] for a in document['agents'] if a['status'] == 'arrived']),
            }
            documents[variant] = document
        first = documents[VARIANTS[0][0]]
        for document in documents.values():
            assert document['requests'] == first['requests']
            assert document['requests_sha256'] == first['requests_sha256']
            assert document['source_sha256'] == first['source_sha256']
            assert document['configuration']['seed'] == first['configuration']['seed']
        matched = sorted(set.intersection(*(set(f) for f in frames.values())))
        assert matched, 'No common all-active timing window'
        for variant, entry in entries.items():
            entry['matched_tick_cpu_ms'] = distribution([float(frames[variant][k]['cpu_ms']) for k in matched])
            entry['matched_tick_elapsed_ms'] = distribution([float(frames[variant][k]['total_ms']) for k in matched])
            rows.append({'trips': cohort, 'variant': variant, 'seed': first['configuration']['seed'],
                         **entry['statuses'], 'updates': entry['updates'],
                         'matched_ticks': len(matched), 'mean_tick_cpu_ms': entry['matched_tick_cpu_ms']['mean'],
                         'p95_tick_cpu_ms': entry['matched_tick_cpu_ms']['p95'],
                         'whole_trip_cpu_ms': entry['whole_trip_cpu_ms'],
                         'whole_trip_elapsed_ms': entry['whole_trip_elapsed_ms'],
                         'stalled_remaining_median_m': entry['stalled_remaining_m']['median'] if entry['stalled_remaining_m'] else '',
                         'audited_segments': entry['audit']['segments'], **entry['steering_stats']})
        paired = []
        for (before, _, _), (after, _, _) in zip(VARIANTS, VARIANTS[1:]):
            a = {r['id']: r for r in documents[before]['agents']}
            b = {r['id']: r for r in documents[after]['agents']}
            success_a = {i for i in a if a[i]['status'] == 'arrived'}
            success_b = {i for i in b if b[i]['status'] == 'arrived'}
            common = sorted(success_a & success_b)
            paired.append({'before': before, 'after': after, 'both_arrive': len(common),
                           'gained': len(success_b-success_a), 'lost': len(success_a-success_b),
                           'gained_ids': sorted(success_b-success_a), 'lost_ids': sorted(success_a-success_b),
                           'common_success_travel_change_percent': distribution([
                               100*(b[i]['travel_m']/a[i]['travel_m']-1) for i in common]),
                           'common_success_arrival_change_percent': distribution([
                               100*(b[i]['arrival_seconds']/a[i]['arrival_seconds']-1) for i in common])})
        result['scenarios'][cohort] = {'variants': entries, 'paired_steps': paired,
            'requests_sha256': first['requests_sha256'], 'seed': first['configuration']['seed'],
            'matched_all_active_ticks': len(matched)}
    assert result['scenarios']['training']['requests_sha256'] != result['scenarios']['second-seed']['requests_sha256']
    (directory/'comparison.json').write_text(json.dumps(result, indent=2)+'\n')
    with (directory/'measurements.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(rows)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    compare(args.directory.resolve())
    print(args.directory/'comparison.json')
