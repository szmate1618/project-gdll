#!/usr/bin/env python3
"""Compare six fresh audited runs after locking exact-building tangent parameters.

This module reads retained evidence only. It never invokes a simulator or any
benchmark executable. Timing is exploratory single-pass update-batch timing;
different configurations can follow different trajectories and active lifetimes.
"""
import argparse
from collections import Counter
import csv
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

import numpy as np
import shapely

from prepare_potential_surface_requests import PhysicalGeometry


ROOT = Path(__file__).resolve().parents[1]
COHORTS = ('training', 'second-seed', 'holdout')
VARIANTS = ('baseline', 'tuned')
SEEDS = {'training': 1515146562, 'second-seed': 1515146563, 'holdout': 1515146564}
STATUSES = ('arrived', 'stalled', 'timeout', 'invalid_endpoint')
BASELINE = (1448.1546878700494, 2., 12.)
CONTACT_THRESHOLD = 1e-5


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


def distribution(values):
    values = list(values)
    if not values:
        return None
    require(all(math.isfinite(v) for v in values), 'Nonfinite diagnostic value')
    return {'count': len(values), 'min': min(values), 'median': statistics.median(values),
            'mean': statistics.mean(values), 'p90': float(np.percentile(values, 90)),
            'p95': float(np.percentile(values, 95)), 'max': max(values)}


def parameters(configuration):
    return tuple(float(configuration[key]) for key in ('weight', 'length_scale', 'support'))


def timestamp(value):
    result = datetime.fromisoformat(value.replace('Z', '+00:00'))
    require(result.tzinfo is not None, 'Provenance timestamps must include a timezone')
    return result


def write_csv(path, rows):
    names = list(dict.fromkeys(key for row in rows for key in row))
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, names, lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def paired_outcomes(before, after):
    require(set(before) == set(after), 'Paired agents have different IDs')
    first = {i for i, agent in before.items() if agent['status'] == 'arrived'}
    last = {i for i, agent in after.items() if agent['status'] == 'arrived'}
    common = sorted(first & last)
    rescued, lost = sorted(last-first), sorted(first-last)
    travel = [after[i]['travel_m']/before[i]['travel_m'] for i in common if before[i]['travel_m'] > 0]
    arrival = [after[i]['arrival_seconds']/before[i]['arrival_seconds'] for i in common
               if before[i]['arrival_seconds'] > 0]
    return {'both_arrive': len(common), 'rescued': len(rescued), 'lost': len(lost),
            'neither_arrives': len(before)-len(first | last),
            'net_arrivals': len(last)-len(first), 'common_arrived_ids': common,
            'rescued_ids': rescued, 'lost_ids': lost,
            'common_success_travel_ratio': distribution(travel),
            'common_success_travel_change_percent': distribution(100*(x-1) for x in travel),
            'common_success_arrival_ratio': distribution(arrival),
            'common_success_arrival_change_percent': distribution(100*(x-1) for x in arrival)}


def contact_diagnostics(agents, geometry):
    """Original OBB/circle clearance; the threshold labels contacts only."""
    result = {'threshold_m': CONTACT_THRESHOLD, 'threshold_is_not_collision_tolerance': True,
              'geometry': 'Original closed padded OBBs, circular trunks and strict inset map bounds; no radius shrink.',
              'by_status': {}}
    for status in ('stalled', 'timeout'):
        selected = [agent for agent in agents if agent['status'] == status]
        counts = dict.fromkeys(('building', 'tree', 'fence', 'map_boundary', 'off_contact'), 0)
        for agent in selected:
            position = np.asarray(agent['final'], dtype=np.float64)
            edge = float(min(np.min(position-geometry.minimum), np.min(geometry.maximum-position)))
            best = (edge, 'map_boundary')
            # A 1 m broad phase suffices for the 10 micrometer label. It need
            # not find the globally nearest obstacle for an off-contact point.
            candidates = geometry.tree.query(shapely.box(position[0]-1, position[1]-1,
                                                         position[0]+1, position[1]+1))
            for index in candidates:
                offset = position-geometry.centers[index]
                if geometry.kinds[index] == 1:
                    distance = float(np.linalg.norm(offset)-geometry.radii[index])
                else:
                    local = np.array([np.dot(offset, geometry.axes[index]),
                                      np.dot(offset, geometry.perpendiculars[index])])
                    distance = float(np.linalg.norm(np.maximum(np.abs(local)-geometry.halves[index], 0)))
                kind = ('building', 'tree', 'fence')[int(geometry.kinds[index])]
                if distance < best[0]:
                    best = (distance, kind)
            counts[best[1] if best[0] <= CONTACT_THRESHOLD else 'off_contact'] += 1
        result['by_status'][status] = {'agents': len(selected), 'counts': counts,
            'with_clipped_moves': sum(agent['clipped_moves'] > 0 for agent in selected),
            'zero_clipped_moves': sum(agent['clipped_moves'] == 0 for agent in selected)}
    return result


def read_ticks(path, measured):
    first, cpu, elapsed, active_total, count = {}, 0., 0., 0, 0
    with path.open() as stream:
        for row in csv.DictReader(stream):
            repeat, step, active = (int(row[key]) for key in ('repeat', 'step', 'active'))
            require(repeat == 0 and step == count, 'Expected one consecutive measured timestep sequence')
            require(0 < active <= 1000, 'Invalid active population in timing CSV')
            costs = [float(row[key]) for key in ('cpu_ms', 'total_ms')]
            require(all(math.isfinite(value) and value >= 0 for value in costs), 'Invalid timestep cost')
            cpu += costs[0]
            elapsed += costs[1]
            active_total += active
            if step < 100 and active == 1000:
                first[(repeat, step)] = {'cpu_ms': costs[0], 'total_ms': costs[1]}
            count += 1
    require(count == measured['steps'] and active_total == measured['updates'], 'Timing CSV count/update mismatch')
    require(math.isclose(cpu, measured['cpu_ms'], rel_tol=1e-10, abs_tol=1e-4), 'Timing CSV CPU sum mismatch')
    require(math.isclose(elapsed, measured['total_ms'], rel_tol=1e-10, abs_tol=1e-4), 'Timing CSV elapsed sum mismatch')
    return first


def compare(output: Path):
    output = Path(output).resolve()
    fingerprints = {}

    def bound(path):
        path = Path(path).resolve()
        value = sha(path)
        require(path not in fingerprints or fingerprints[path] == value, 'Input changed during comparison')
        fingerprints[path] = value
        return value

    def read(path):
        bound(path)
        return json.loads(Path(path).read_text())

    plan = read(output/'plan.json')
    winner_hash = bound(output/'winner.json')
    winner = read(output/'winner.json')
    final_inputs = read(output/'final-inputs.json')
    require(plan['seeds'] == SEEDS, 'Unexpected predeclared cohort seeds')
    require(plan['final_repeats'] == 1 and plan['final_warmups'] == 0, 'Expected one fresh pass and no warmup')
    require(parameters(plan['baseline']) == BASELINE, 'Baseline is not the current exact-building tangent configuration')
    tuned = parameters(winner['configuration'])
    require(all(math.isfinite(value) for value in tuned) and tuned[0] >= 0 and min(tuned[1:]) > 0,
            'Invalid locked configuration')
    require(winner['holdout_seed'] == SEEDS['holdout'], 'Locked holdout seed differs from predeclared seed')
    require(final_inputs['locked_winner_sha256'] == winner_hash, 'Final inputs are not bound to the winner lock')
    require(final_inputs['winner_locked_at_utc'] == winner['locked_at_utc'], 'Winner lock timestamp differs')
    require(timestamp(final_inputs['holdout_generated_at_utc']) >= timestamp(winner['locked_at_utc']),
            'Holdout generation predates the configuration lock')
    require(bound(plan['scene']) == plan['scene_sha256'], 'Original scene changed')
    require(bound(plan['binary']) == plan['binary_sha256'], 'Benchmark binary changed or original name not restored')
    require(isinstance(plan['source_sha256'], dict), 'Expected frozen source fingerprint map')
    for relative, expected in plan['source_sha256'].items():
        require(bound(ROOT/relative) == expected, 'Frozen runtime source changed: '+relative)
    geometry = PhysicalGeometry(read(plan['scene']))
    source = plan['scene_sha256']
    requests = {}
    for cohort in COHORTS:
        record = final_inputs['requests'][cohort]
        require(record['seed'] == SEEDS[cohort] and bound(record['file']) == record['sha256'],
                'Final request provenance mismatch: '+cohort)
        document = read(record['file'])
        require(document['seed'] == SEEDS[cohort] and document['source_sha256'] == source,
                'Request seed/source mismatch: '+cohort)
        requests[cohort] = document['requests']
    require(len({final_inputs['requests'][cohort]['sha256'] for cohort in COHORTS}) == 3,
            'The three cohorts must retain different request bytes')
    declared = {(case['cohort'], case['variant']): case for case in final_inputs['cases']}
    require(len(declared) == len(final_inputs['cases']) == 6 and
            set(declared) == {(c, v) for c in COHORTS for v in VARIANTS}, 'Expected six distinct fresh final cases')
    result = {'schema': 1, 'source_sha256': source,
              'baseline_configuration': plan['baseline'], 'locked_configuration': winner['configuration'],
              'winner_sha256': winner_hash, 'winner_locked_at_utc': winner['locked_at_utc'],
              'holdout_generated_at_utc': final_inputs['holdout_generated_at_utc'],
              'cohort_scope': {'training': 'Reused seed used for parameter selection.',
                              'second-seed': 'Previously studied seed, not used for selection in this sweep; not a pristine holdout.',
                              'holdout': 'Fresh predeclared seed, generated after winner lock; not used for selection.'},
              'timing_scope': 'Single fresh measured pass per case, zero warmups. Matched steps0-99 keep1000agents active in each paired run, but trajectories may differ. Setup, traces, movement logging and audits are outside update-batch timers. No statistical speed ranking.',
              'hash_scope': 'Metadata, scene, binary, frozen sources, request bytes, summaries, audits and timing CSVs rehashed before/after comparison. Movement streams are bound by independent audit SHA, byte count and format header; the comparison does not rerun the movement audit.',
              'scenarios': {}}
    rows, paired_rows, contact_rows = [], [], []
    total_segments = 0
    for cohort in COHORTS:
        documents, agents_by_variant, frames, entries = {}, {}, {}, {}
        original = {int(row['id']): row for row in requests[cohort]}
        require(len(original) == len(requests[cohort]) == 1000, 'Expected1000unique requests')
        for variant in VARIANTS:
            case = output/'final'/f'{cohort}-{variant}'
            descriptor = declared[cohort, variant]
            require(Path(descriptor['output']).resolve() == case, 'Unexpected final case output path')
            require(descriptor['name'] == case.name and descriptor['requests_sha256'] == final_inputs['requests'][cohort]['sha256'],
                    'Final case descriptor differs from retained inputs')
            expected = BASELINE if variant == 'baseline' else tuned
            require(parameters(descriptor['configuration']) == expected, 'Case descriptor configuration differs from lock')
            manifest = read(case/'manifest.json')
            execution = manifest['benchmark_executable']
            original_binary = Path(execution['original']).resolve()
            running_binary = Path(execution['during_run']).resolve()
            basename = running_binary.name.casefold()
            require(original_binary == case/'binaries/benchmark_potential_surface' and
                    running_binary.parent == original_binary.parent and running_binary != original_binary,
                    'Final benchmark did not use its isolated renamed executable')
            require(('agent' in basename or 'npc' in basename) and 'zombie' not in basename,
                    'Benchmark process basename violates the agent/npc naming rule')
            require(execution['restored_after_run'] is True and original_binary.is_file() and
                    not running_binary.exists() and not running_binary.is_symlink(),
                    'Final benchmark executable was not restored after execution')
            require(manifest['binary_sha256'] == plan['binary_sha256'] == bound(original_binary),
                    'Final benchmark executable bytes differ from the frozen binary')
            require(manifest['command'].count(str(running_binary)) == 1 and
                    str(original_binary) not in manifest['command'],
                    'Recorded final command did not execute the renamed benchmark exactly once')
            document = read(case/'simulation/summary.json')
            for agent in document['agents']:
                agent.pop('trace', None)
            audit = read(case/'audit.json')
            config, measured = document['configuration'], document['summary']
            require(document['native_validation_passed'] and document['deterministic'] and audit['passed'],
                    'Native validation/determinism or independent audit failed: '+case.name)
            require(config['repeats'] == len(document['samples']) == 1 and config['warmups'] == 0,
                    'A final case is not a fresh single pass')
            sample = document['samples'][0]
            require(sample['repeat'] == 0 and all(sample[key] == measured[key] for key in
                    ('statuses', 'updates', 'moved_segments', 'validation_segments', 'validation_violations',
                     'invalid_moves', 'clipped_moves', 'cpu_ms', 'total_ms', 'query_stats', 'steering_stats')),
                    'Summary differs from its single measured sample')
            require(config['exact_buildings'] and config['steering_mode'] == 'tangent' and config['guidance_mode'] == 'none',
                    'Retuning must retain exact buildings, tangent steering and no structural guidance')
            require((config['weight'], config['length_scale_m'], config['support_m']) == expected,
                    'Measured field configuration differs from the lock')
            require((config['timestep_seconds'], config['speed_m_per_second'], config['radius_m'], config['skin_m'],
                     config['building_extra_m'], config['arrival_radius_m'], config['stall_window_seconds'],
                     config['stall_anchor_displacement_m']) == (.05, 2, .3, .002, .05, .15, 10, .05),
                    'Retuning changed fixed physics, arrival or stall rules')
            require(config['seed'] == SEEDS[cohort] and not config['navmesh'] and not config['global_paths'] and
                    not config['agent_agent_collisions'] and config['record_moves'], 'Unexpected final case behavior')
            guidance = document['guidance']
            require(guidance['mode'] == 'none' and guidance['bumps'] == guidance['pockets'] == measured['guidance_bytes'] == 0,
                    'Unexpected structural guidance allocation')
            require(not guidance['file'] and not guidance['sha256'] and all(value == 0 for value in measured['guidance_stats'].values()),
                    'Disabled structural guidance performed work')
            require(document['source_sha256'] == audit['source_sha256'] == source, 'Case source fingerprint mismatch')
            require(bound(document['requests_file']) == document['requests_sha256'] == descriptor['requests_sha256'] and
                    document['requests'] == requests[cohort], 'Paired request bytes or original endpoints changed')
            require(measured['population'] == len(document['agents']) == 1000 and
                    measured['statuses']['invalid_endpoint'] == measured['invalid_moves'] == measured['validation_violations'] == 0,
                    'Invalid endpoints or native physical violations')
            for key in ('colliding_pairs', 'colliding_segments', 'map_boundary_violations', 'invalid_record_ids',
                        'invalid_endpoints', 'nonfinite_records', 'discontinuous_segments', 'final_position_mismatches',
                        'invalid_final_positions', 'record_count_mismatch'):
                require(audit[key] == 0, 'Independent movement audit error: '+key)
            moves = Path(document['moves_file'])
            moves = moves if moves.is_absolute() else case/'simulation'/moves
            require(moves.resolve() == Path(audit['moves_file']).resolve() and moves.is_file(), 'Movement stream path mismatch')
            require(re.fullmatch('[0-9a-f]{64}', audit['moves_sha256']) is not None and audit['record_bytes'] == 36,
                    'Invalid retained movement fingerprint/format')
            require(audit['segments'] == measured['moved_segments'] == document['moves_records'] and
                    moves.stat().st_size == 8+36*audit['segments'], 'Movement stream byte/record count mismatch')
            with moves.open('rb') as stream:
                require(stream.read(8) == b'PSSMOV01', 'Movement stream header mismatch')
            agents = {int(agent['id']): agent for agent in document['agents']}
            require(len(agents) == len(document['agents']) and set(agents) == set(original), 'Final agent IDs differ from requests')
            require(dict(Counter(agent['status'] for agent in agents.values())) ==
                    {key: value for key, value in measured['statuses'].items() if value}, 'Status totals differ from agent records')
            require(sum(agent['updates'] for agent in agents.values()) == measured['updates'] == measured['validation_segments'],
                    'Native update/validation counts differ')
            require(sum(agent['clipped_moves'] for agent in agents.values()) == measured['clipped_moves'],
                    'Agent clipping counts differ from summary')
            for key, value in measured['steering_stats'].items():
                require(sum(agent['steering_stats'][key] for agent in agents.values()) == value,
                        'Agent steering counts differ from summary: '+key)
            for index, agent in agents.items():
                request = original[index]
                require(agent['start'] == request['start'] and agent['goal'] == request['goal'] and agent['status'] in STATUSES,
                        'Agent endpoint or status changed')
                require(all(math.isfinite(x) for key in ('start', 'goal', 'final') for x in agent[key]), 'Nonfinite agent coordinates')
                require(math.isclose(math.dist(agent['start'], agent['goal']), agent['initial_distance_m'], rel_tol=1e-12, abs_tol=1e-9),
                        'Initial distance differs from exact endpoints')
                require(math.isclose(agent['budget_seconds'], max(120, 3*agent['initial_distance_m']/2+60), rel_tol=1e-12),
                        'Per-agent time budget changed')
                require(math.isfinite(agent['travel_m']) and agent['travel_m'] >= 0 and agent['invalid_moves'] == 0,
                        'Invalid final travel/physical record')
                require(math.isfinite(agent['elapsed_seconds']) and 0 <= agent['elapsed_seconds'] <= agent['budget_seconds']+.05+1e-8,
                        'Agent elapsed time exceeds unchanged budget')
                require(agent['travel_m'] <= agent['updates']*.1+1e-6, 'Recorded travel exceeds fixed speed cap')
                if agent['status'] == 'arrived':
                    require(math.dist(agent['final'], agent['goal']) <= .15 and agent['arrival_seconds'] is not None,
                            'Arrival lies outside the fixed arrival radius')
                    require(agent['arrival_seconds'] == agent['elapsed_seconds'], 'Arrival time differs from terminal update')
                if agent['status'] == 'timeout':
                    require(agent['elapsed_seconds'] >= agent['budget_seconds'], 'Timeout precedes budget exhaustion')
            bound(case/'simulation/steps.csv')
            frames[variant] = read_ticks(case/'simulation/steps.csv', measured)
            normalized = [math.dist(a['final'], a['goal'])/a['initial_distance_m'] if a['initial_distance_m'] else 0.
                          for a in agents.values()]
            contacts = contact_diagnostics(list(agents.values()), geometry)
            entry = {'configuration': config, 'statuses': measured['statuses'], 'updates': measured['updates'],
                     'whole_trip_cpu_ms': measured['cpu_ms'], 'whole_trip_elapsed_ms': measured['total_ms'],
                     'cpu_ns_per_update': measured['cpu_ms']*1e6/measured['updates'] if measured['updates'] else None,
                     'world_setup_ms': document['setup_world_ms'], 'world_bytes': measured['world_bytes'],
                     'scratch_bytes': measured['simulation_scratch_bytes'], 'guidance_bytes': 0,
                     'query_stats': measured['query_stats'], 'steering_stats': measured['steering_stats'],
                     'clipped_moves': measured['clipped_moves'], 'audit': audit,
                     'summary_file': str(case/'simulation/summary.json'),
                     'whole_cohort_normalized_remaining': distribution(normalized),
                     'clipped_normalized_remaining_mean': statistics.mean(min(1., value) for value in normalized),
                     'contact_diagnostic': contacts,
                     'remaining_m_by_status': {status: distribution(math.dist(a['final'], a['goal']) for a in agents.values() if a['status'] == status)
                                               for status in ('stalled', 'timeout')},
                     'arrived_travel_to_direct_ratio': distribution(a['travel_m']/a['initial_distance_m'] for a in agents.values()
                                                                  if a['status'] == 'arrived' and a['initial_distance_m'] > 0)}
            total_segments += audit['segments']
            entries[variant], documents[variant], agents_by_variant[variant] = entry, document, agents
        expected_ticks = {(0, step) for step in range(100)}
        require(set(frames['baseline']) == set(frames['tuned']) == expected_ticks,
                'Expected exactly the same first100all-active timestep keys')
        for variant, entry in entries.items():
            entry['matched_tick_cpu_ms'] = distribution(frames[variant][key]['cpu_ms'] for key in sorted(expected_ticks))
            entry['matched_tick_elapsed_ms'] = distribution(frames[variant][key]['total_ms'] for key in sorted(expected_ticks))
            rows.append({'trips': cohort, 'variant': variant, 'seed': SEEDS[cohort],
                'weight': entry['configuration']['weight'], 'length_scale_m': entry['configuration']['length_scale_m'],
                'support_m': entry['configuration']['support_m'], **entry['statuses'], 'updates': entry['updates'],
                'matched_ticks': 100, 'mean_tick_cpu_ms': entry['matched_tick_cpu_ms']['mean'],
                'p95_tick_cpu_ms': entry['matched_tick_cpu_ms']['p95'], 'whole_trip_cpu_ms': entry['whole_trip_cpu_ms'],
                'whole_trip_elapsed_ms': entry['whole_trip_elapsed_ms'], 'cpu_ns_per_update': entry['cpu_ns_per_update'],
                'world_bytes': entry['world_bytes'], 'scratch_bytes': entry['scratch_bytes'],
                'audited_segments': entry['audit']['segments'], 'clipped_moves': entry['clipped_moves'],
                'mean_clipped_normalized_remaining': entry['clipped_normalized_remaining_mean'],
                'mean_unclipped_normalized_remaining': entry['whole_cohort_normalized_remaining']['mean'],
                'median_normalized_remaining': entry['whole_cohort_normalized_remaining']['median'],
                **{'query_'+key: value for key, value in entry['query_stats'].items()}})
            for status, contact in entry['contact_diagnostic']['by_status'].items():
                contact_rows.append({'trips': cohort, 'variant': variant, 'status': status,
                                     'agents': contact['agents'], **contact['counts'],
                                     'with_clipped_moves': contact['with_clipped_moves'],
                                     'zero_clipped_moves': contact['zero_clipped_moves']})
        paired = paired_outcomes(agents_by_variant['baseline'], agents_by_variant['tuned'])
        for variant, agents in agents_by_variant.items():
            common = [agents[index] for index in paired['common_arrived_ids']]
            entries[variant]['common_success'] = {'count': len(common),
                'travel_m': distribution(a['travel_m'] for a in common),
                'arrival_seconds': distribution(a['arrival_seconds'] for a in common),
                'travel_to_direct_ratio': distribution(a['travel_m']/a['initial_distance_m'] for a in common if a['initial_distance_m'] > 0)}
        paired_rows.append({'trips': cohort, 'seed': SEEDS[cohort],
            **{key: paired[key] for key in ('both_arrive', 'rescued', 'lost', 'neither_arrives', 'net_arrivals')},
            'median_common_success_travel_ratio': paired['common_success_travel_ratio']['median'] if paired['common_success_travel_ratio'] else '',
            'median_common_success_arrival_ratio': paired['common_success_arrival_ratio']['median'] if paired['common_success_arrival_ratio'] else ''})
        result['scenarios'][cohort] = {'seed': SEEDS[cohort],
            'requests_sha256': final_inputs['requests'][cohort]['sha256'], 'matched_all_active_ticks': 100,
            'variants': entries, 'paired': paired}
    result['all_six_audits_passed'] = True
    result['total_audited_segments'] = total_segments
    for path, expected in fingerprints.items():
        require(sha(path) == expected, 'Input changed during comparison: '+str(path))
    result['input_sha256'] = {str(path): value for path, value in fingerprints.items()}
    (output/'comparison.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    write_csv(output/'measurements.csv', rows)
    write_csv(output/'paired-outcomes.csv', paired_rows)
    write_csv(output/'contacts.csv', contact_rows)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    compare(args.directory)
    print(args.directory/'comparison.json')
