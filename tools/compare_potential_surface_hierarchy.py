#!/usr/bin/env python3
"""Compare PSS guidance ablations with paired outcomes and prior reachability strata."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

import numpy as np

VARIANTS = (('baseline', 'none'), ('hierarchy', 'hierarchy'),
            ('pockets', 'pockets'), ('combined', 'combined'))
COHORTS = ('training', 'second-seed')
STATUSES = ('arrived', 'stalled', 'timeout', 'invalid_endpoint')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def distribution(values):
    if not values:
        return None
    return {'count': len(values), 'min': min(values), 'median': statistics.median(values),
            'mean': statistics.mean(values), 'p90': float(np.percentile(values, 90)),
            'p95': float(np.percentile(values, 95)), 'max': max(values)}


def reachability_group(evidence):
    if evidence == 'unreachable_buildings_fences':
        return 'known_impossible'
    if evidence in ('reachable_navmesh_witness', 'reachable_recorded_arrival'):
        return 'known_reachable'
    if evidence == 'unresolved':
        return 'unresolved'
    raise ValueError(f'Unknown retained reachability category: {evidence}')


def paired_outcomes(before, after, strata):
    a, b = {r['id']: r for r in before}, {r['id']: r for r in after}
    assert set(a) == set(b)
    first = {i for i in a if a[i]['status'] == 'arrived'}
    last = {i for i in b if b[i]['status'] == 'arrived'}
    common = sorted(first & last)
    gained, lost = sorted(last-first), sorted(first-last)
    changes = {}
    for group in ('known_reachable', 'known_impossible', 'unresolved'):
        ids = {i for i in a if reachability_group(strata[i]['classification']) == group}
        changes[group] = {'agents': len(ids), 'both_arrive': len(set(common) & ids),
                          'rescued': len(set(gained) & ids), 'lost': len(set(lost) & ids)}
    ratios = [b[i]['travel_m']/a[i]['travel_m'] for i in common if a[i]['travel_m'] > 0]
    times = [b[i]['arrival_seconds']/a[i]['arrival_seconds'] for i in common if a[i]['arrival_seconds'] > 0]
    return {'both_arrive': len(common), 'rescued': len(gained), 'lost': len(lost),
            'rescued_ids': gained, 'lost_ids': lost, 'reachability_strata': changes,
            'common_success_travel_ratio': distribution(ratios),
            'common_success_travel_change_percent': distribution([100*(x-1) for x in ratios]),
            'common_success_arrival_ratio': distribution(times),
            'common_success_arrival_change_percent': distribution([100*(x-1) for x in times])}


def summarize_strata(agents, evidence):
    result = {}
    for group in ('known_reachable', 'known_impossible', 'unresolved'):
        selected = [a for a in agents if reachability_group(evidence[a['id']]['classification']) == group]
        statuses = {status: sum(a['status'] == status for a in selected) for status in STATUSES}
        subtypes = {}
        for a in selected:
            subtype = evidence[a['id']]['classification']
            subtypes[subtype] = subtypes.get(subtype, 0)+1
        result[group] = {'agents': len(selected), 'statuses': statuses,
                         'completion_fraction': statuses['arrived']/len(selected) if selected else None,
                         'retained_evidence': subtypes,
                         'remaining_m': distribution([math.dist(a['final'], a['goal']) for a in selected]),
                         'failed_remaining_m': distribution([math.dist(a['final'], a['goal']) for a in selected if a['status'] != 'arrived'])}
    assert result['known_impossible']['statuses']['arrived'] == 0, 'Arrival contradicts retained physical disconnection'
    return result


def numeric_agent_engagement(agents, key):
    names = {name for a in agents for name, value in a.get(key, {}).items()
             if isinstance(value, (int, float)) and not isinstance(value, bool)}
    return {name: sum(a.get(key, {}).get(name, 0) > 0 for a in agents) for name in sorted(names)}


def write_csv(path, rows):
    names = list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=names, lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def compare(directory):
    directory = Path(directory).resolve()
    plan = json.loads((directory/'plan.json').read_text())
    parameters = plan['parameters']
    prior_path = Path(plan['reachability']['file'])
    assert sha(prior_path) == plan['reachability']['sha256']
    prior = json.loads(prior_path.read_text())
    assert prior['source_sha256'] == plan['scene_sha256']
    evidence = {}
    for row in prior['agents']:
        key = (row['trips'], int(row['id']))
        assert key not in evidence, 'Duplicate retained classification ID'
        evidence[key] = row
    result = {'schema': 1, 'parameters': parameters, 'guidance': plan['guidance'],
              'timing_scope': plan['timing_scope'], 'validation_scope': plan['validation_scope'],
              'prior_reachability_sha256': sha(prior_path),
              'strata_interpretation': 'Known impossible means disconnected in exact declared building/fence free space; known reachable preserves prior full physical-route witnesses or recorded arrivals at the experiment arrival radius. Unresolved remains unclassified. No endpoint or stratum is removed.',
              'scenarios': {}}
    rows, stratum_rows = [], []
    for cohort in COHORTS:
        documents, frames, entries = {}, {}, {}
        retained = {idx: row for (trips, idx), row in evidence.items() if trips == cohort}
        assert len(retained) == 1000
        for variant, mode in VARIANTS:
            case = directory/f'{cohort}-{variant}'
            document = json.loads((case/'simulation/summary.json').read_text())
            # Histories are large and irrelevant to paired outcome arithmetic.
            # All original histories remain intact in each frozen source JSON.
            for agent in document['agents']:
                agent.pop('trace', None)
            audit = json.loads((case/'audit.json').read_text())
            config, measured = document['configuration'], document['summary']
            guidance = document['guidance']
            assert math.isfinite(document['setup_world_ms']) and document['setup_world_ms'] >= 0
            assert math.isfinite(guidance['load_wall_ms']) and guidance['load_wall_ms'] >= 0
            assert measured['guidance_bytes'] == guidance['memory_bytes']
            assert document['native_validation_passed'] and document['deterministic'] and audit['passed']
            assert len(document['samples']) == plan['repeats'] == 1 and config['warmups'] == 0
            assert measured['population'] == len(document['agents']) == 1000
            assert measured['statuses']['invalid_endpoint'] == measured['invalid_moves'] == measured['validation_violations'] == 0
            assert config['exact_buildings'] and config['steering_mode'] == 'tangent'
            assert guidance.get('mode', config.get('guidance_mode')) == mode
            assert (config['weight'], config['length_scale_m'], config['support_m']) == (
                parameters['weight'], parameters['length_scale'], parameters['support'])
            assert (config['timestep_seconds'], config['speed_m_per_second'], config['radius_m'],
                    config['skin_m'], config['building_extra_m'], config['arrival_radius_m']) == (.05, 2, .3, .002, .05, .15)
            assert not config['navmesh'] and not config['global_paths'] and not config['agent_agent_collisions']
            assert document['source_sha256'] == audit['source_sha256'] == plan['scene_sha256']
            assert audit['segments'] == measured['moved_segments']
            assert document['moves_file'] and config['record_moves']
            if mode == 'none':
                assert measured['guidance_bytes'] == guidance['bumps'] == guidance['pockets'] == 0
                assert not guidance['file'] and not guidance['sha256']
                assert all(value == 0 for value in measured['guidance_stats'].values())
            else:
                assert guidance['sha256'] == plan['guidance']['sha256']
                if mode == 'hierarchy':
                    assert guidance['pockets'] == 0
                if mode == 'pockets':
                    assert guidance['bumps'] == 0
            agents = {int(a['id']): a for a in document['agents']}
            assert len(agents) == len(document['agents']) and set(agents) == set(retained)
            for idx, agent in agents.items():
                old = retained[idx]
                assert agent['status'] in STATUSES
                assert math.isclose(agent['initial_distance_m'], old['initial_direct_m'], rel_tol=0, abs_tol=1e-9)
                if agent['status'] == 'arrived':
                    assert math.dist(agent['final'], agent['goal']) <= .15
            with (case/'simulation/steps.csv').open() as stream:
                fixed = [r for r in csv.DictReader(stream) if int(r['step']) < 100 and int(r['active']) == 1000]
            frames[variant] = {(int(r['repeat']), int(r['step'])): r for r in fixed}
            assert len(frames[variant]) == len(fixed)
            failed = [a for a in agents.values() if a['status'] != 'arrived']
            strata = summarize_strata(list(agents.values()), retained)
            entries[variant] = {
                'statuses': measured['statuses'], 'updates': measured['updates'],
                'whole_trip_cpu_ms': measured['cpu_ms'], 'whole_trip_elapsed_ms': measured['total_ms'],
                'world_setup_ms': document.get('setup_world_ms'),
                'guidance_load_ms': guidance.get('load_wall_ms', 0), 'guidance': guidance,
                'world_bytes': measured['world_bytes'],
                'guidance_bytes': measured.get('guidance_bytes', guidance.get('memory_bytes', 0)),
                'scratch_bytes': measured['simulation_scratch_bytes'],
                'steering_stats': measured.get('steering_stats', {}),
                'guidance_stats': measured.get('guidance_stats', {}),
                'guidance_agent_engagement': numeric_agent_engagement(list(agents.values()), 'guidance_stats'),
                'query_stats': measured['query_stats'], 'clipped_moves': measured['clipped_moves'],
                'audit': audit, 'reachability_strata': strata,
                'failed_zero_clips_by_status': {status: sum(a['status'] == status and a['clipped_moves'] == 0 for a in failed)
                                               for status in ('stalled', 'timeout')},
                'stalled_remaining_m': distribution([math.dist(a['final'], a['goal']) for a in failed if a['status'] == 'stalled']),
                'timeout_remaining_m': distribution([math.dist(a['final'], a['goal']) for a in failed if a['status'] == 'timeout']),
                'arrived_travel_to_direct_ratio': distribution([a['travel_m']/a['initial_distance_m'] for a in agents.values()
                                                                if a['status'] == 'arrived' and a['initial_distance_m'] > 0]),
            }
            documents[variant] = document
        baseline = documents['baseline']
        for document in documents.values():
            assert document['requests'] == baseline['requests']
            assert document['requests_sha256'] == baseline['requests_sha256']
            assert document['configuration']['seed'] == baseline['configuration']['seed']
        expected_ticks = {(0, step) for step in range(100)}
        matched = set.intersection(*(set(value) for value in frames.values()))
        assert matched == expected_ticks, 'Expected exactly the same first 100 all-active ticks'
        matched = sorted(matched)
        for variant, entry in entries.items():
            entry['matched_tick_cpu_ms'] = distribution([float(frames[variant][key]['cpu_ms']) for key in matched])
            entry['matched_tick_elapsed_ms'] = distribution([float(frames[variant][key]['total_ms']) for key in matched])
            row = {'trips': cohort, 'variant': variant, 'seed': baseline['configuration']['seed'],
                   **entry['statuses'], 'updates': entry['updates'], 'matched_ticks': len(matched),
                   'mean_tick_cpu_ms': entry['matched_tick_cpu_ms']['mean'],
                   'p95_tick_cpu_ms': entry['matched_tick_cpu_ms']['p95'],
                   'whole_trip_cpu_ms': entry['whole_trip_cpu_ms'],
                   'whole_trip_elapsed_ms': entry['whole_trip_elapsed_ms'],
                   'world_setup_ms': entry['world_setup_ms'], 'guidance_load_ms': entry['guidance_load_ms'],
                   'world_bytes': entry['world_bytes'], 'guidance_bytes': entry['guidance_bytes'],
                   'scratch_bytes': entry['scratch_bytes'], 'audited_segments': entry['audit']['segments']}
            for prefix, stats in [('steering', entry['steering_stats']), ('guidance', entry['guidance_stats'])]:
                row.update({f'{prefix}_{key}': value for key, value in stats.items() if isinstance(value, (int, float))})
            rows.append(row)
            for name, stratum in entry['reachability_strata'].items():
                stratum_rows.append({'trips': cohort, 'variant': variant, 'stratum': name,
                                     'agents': stratum['agents'], **stratum['statuses'],
                                     'completion_fraction': stratum['completion_fraction']})
        pairs = [('baseline', name) for name, _ in VARIANTS if name != 'baseline']
        pairs += [('hierarchy', 'combined'), ('pockets', 'combined')]
        paired = [{'before': before, 'after': after,
                   **paired_outcomes(documents[before]['agents'], documents[after]['agents'], retained)}
                  for before, after in pairs]
        result['scenarios'][cohort] = {'seed': baseline['configuration']['seed'],
            'requests_sha256': baseline['requests_sha256'], 'matched_all_active_ticks': len(matched),
            'variants': entries, 'paired_changes': paired}
    assert result['scenarios']['training']['requests_sha256'] != result['scenarios']['second-seed']['requests_sha256']
    (directory/'comparison.json').write_text(json.dumps(result, indent=2)+'\n')
    write_csv(directory/'measurements.csv', rows)
    write_csv(directory/'reachability-strata.csv', stratum_rows)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    compare(args.directory)
    print(args.directory/'comparison.json')
