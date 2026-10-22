#!/usr/bin/env python3
"""Join exact tree-free connectivity with physically validated navmesh witnesses."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def distribution(values):
    return {'count': len(values), 'min': min(values), 'median': statistics.median(values),
            'max': max(values)} if values else None


def run(args):
    source = json.loads(args.components.read_text())
    checks = [json.loads(p.read_text()) for p in args.navmesh_check]
    rows, scenarios = [], {}
    for cohort in ('training', 'second-seed'):
        summary_path = args.run_directory/f'{cohort}-rectangles-tangent/simulation/summary.json'
        document = json.loads(summary_path.read_text())
        component_agents = {a['id']: a for a in source['cohorts'][cohort]['requests']}
        assert source['source_sha256'] == document['source_sha256']
        cohorts = []
        for check in checks:
            assert check['original_physics']['source_sha256'] == document['source_sha256']
            matching = [s for s in check['summaries'] if Path(s['summary']).resolve() == summary_path.resolve()]
            assert len(matching) == 1
            entry = matching[0]
            assert entry['summary_sha256'] == sha(summary_path)
            cohorts.append({a['id']: a for a in entry['agents']})
        groups = {}
        for agent in document['agents']:
            idx, status = agent['id'], agent['status']
            component = component_agents[idx]
            assert component['start'] == agent['start'] and component['goal'] == agent['goal']
            evidence = []
            for i, candidates in enumerate(cohorts):
                candidate = candidates[idx]
                assert candidate['start'] == agent['start'] and candidate['goal'] == agent['goal']
                if candidate['route_witness'] == 'validated_physical_route':
                    assert candidate['route_endpoints_preserved'] and candidate['route_coordinates_finite']
                    assert candidate['invalid_physical_segments'] == candidate['invalid_navmesh_segments'] == 0
                    points = candidate['route_points']
                    lengths = [math.dist(a, b) for a, b in zip(points, points[1:])]
                    speed = document['configuration']['speed_m_per_second']
                    timestep = document['configuration']['timestep_seconds']
                    framed_time = sum(math.ceil(length/(speed*timestep))*timestep for length in lengths)
                    evidence.append((framed_time, sum(lengths), i))
            impossible = component['relation'] == 'different_components'
            assert not (impossible and (status == 'arrived' or evidence)), 'Contradictory connectivity evidence'
            best = min(evidence) if evidence else None
            category = ('unreachable_buildings_fences' if impossible else
                        'reachable_navmesh_witness' if best else
                        'reachable_recorded_arrival' if status == 'arrived' else 'unresolved')
            row = {'trips': cohort, 'id': idx, 'steering_status': status,
                   'classification': category, 'physical_treefree_relation': component['relation'],
                   'budget_seconds': agent['budget_seconds'], 'initial_direct_m': agent['initial_distance_m'],
                   'actual_travel_m': agent['travel_m'], 'remaining_m': math.dist(agent['final'], agent['goal']),
                   'navmesh_witness_m': best[1] if best else '',
                   'witness_frame_time_seconds': best[0] if best else '',
                   'witness_fits_budget': best[0] <= agent['budget_seconds'] if best else '',
                   'witness_source': str(args.navmesh_check[best[2]]) if best else ''}
            rows.append(row)
            group = groups.setdefault(status, {'agents': 0, 'classification': {}, 'witnesses_fit_budget': 0,
                                               'witnesses_exceed_budget': 0})
            group['agents'] += 1
            group['classification'][category] = group['classification'].get(category, 0)+1
            if best:
                group['witnesses_fit_budget' if row['witness_fits_budget'] else 'witnesses_exceed_budget'] += 1
        for status, group in groups.items():
            subset = [r for r in rows if r['trips'] == cohort and r['steering_status'] == status]
            witnesses = [r for r in subset if r['classification'] == 'reachable_navmesh_witness']
            group['witness_distance_m'] = distribution([r['navmesh_witness_m'] for r in witnesses])
            group['actual_travel_m_with_witness'] = distribution([r['actual_travel_m'] for r in witnesses])
            group['actual_over_witness_distance'] = distribution([
                r['actual_travel_m']/r['navmesh_witness_m'] for r in witnesses if r['navmesh_witness_m'] > 0])
        scenarios[cohort] = {'by_status': groups,
            'failed_agents': sum(r['trips'] == cohort and r['steering_status'] != 'arrived' for r in rows),
            'unreachable': sum(r['trips'] == cohort and r['classification'] == 'unreachable_buildings_fences' for r in rows),
            'unresolved_failures': sum(r['trips'] == cohort and r['classification'] == 'unresolved' for r in rows)}
    report = {'schema': 1, 'source_sha256': source['source_sha256'],
        'inputs': {str(p): sha(p) for p in [args.components, *args.navmesh_check]},
        'script_sha256': sha(__file__),
        'classification': 'Building/fence-only physical disconnection is negative evidence even with trees removed. A full original-endpoint route whose every segment passes PSS collision is positive evidence with trees. Other failed trips stay unresolved.',
        'route_time': 'Conservative waypoint traversal time: sum(ceil(segment_length/(speed*timestep))*timestep), allowing one exact final substep per segment and instantaneous turns as in the experiment. Over-budget weighted witnesses do not exclude shorter routes.',
        'scenarios': scenarios, 'agents': rows}
    args.output_directory.mkdir(parents=True, exist_ok=True)
    (args.output_directory/'comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    with (args.output_directory/'requests.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(rows)
    print(json.dumps(scenarios, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-directory', type=Path, required=True)
    parser.add_argument('--components', type=Path, required=True)
    parser.add_argument('--navmesh-check', type=Path, action='append', required=True)
    parser.add_argument('--output-directory', type=Path, required=True)
    run(parser.parse_args())
