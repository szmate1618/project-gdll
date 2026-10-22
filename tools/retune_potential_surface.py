#!/usr/bin/env python3
"""Retune field parameters while preserving exact-building tangent steering."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import itertools
import json
import math
from pathlib import Path
import queue
import shutil
import statistics
import subprocess
import sys
import threading
import time

from agent_benchmark_process import benchmark_executable
from benchmark_potential_surface_hierarchy import retained_values_equal, source_files
from run_potential_surface_experiment import sha

BASELINE = {'weight': 1448.1546878700494, 'length_scale': 2., 'support': 12.,
            'boundary_strength': 1448.1546878700494 / 4}
SEEDS = {'training': 1515146562, 'second-seed': 1515146563, 'holdout': 1515146564}


def utc():
    return time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())


def write(path, value):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def key(configuration):
    return tuple(round(configuration[name], 10) for name in ('weight', 'length_scale', 'support'))


def parameters(strength, length, support):
    return {'weight': strength * length * length, 'length_scale': length,
            'support': support, 'boundary_strength': strength}


def rank(entry):
    c = entry['configuration']
    return (-entry['arrived'], entry['remaining_distance_ratio_clipped_mean'],
            c['support'], c['length_scale'], c['weight'])


def candidates():
    values = [parameters(k, length, support) for k, length, support in itertools.product(
        (16., 64., 256., 1024.), (1., 2., 4.), (3., 6., 12., 24.))]
    values += [{**BASELINE, 'support': support} for support in (1.5, 3., 6., 12., 24., 48.)]
    values += [parameters(k, 2., 12.) for k in (0., 1.)]
    return list({key(c): c for c in values}.values())


def refinements(centers, existing):
    seen = {key(c) for c in existing}
    result = []
    for center in centers:
        for name, factor in (('boundary_strength', 2.), ('length_scale', math.sqrt(2)),
                             ('support', math.sqrt(2))):
            for multiplier in (1 / factor, factor):
                c = {**center, name: center[name] * multiplier}
                c = parameters(c['boundary_strength'], c['length_scale'], c['support'])
                if (.25 <= c['boundary_strength'] <= 4096 and .5 <= c['length_scale'] <= 8
                        and 1.5 <= c['support'] <= 48 and key(c) not in seen):
                    seen.add(key(c)); result.append(c)
        if center['support'] == 24:
            c = {**center, 'support': 48.}
            if key(c) not in seen:
                seen.add(key(c)); result.append(c)
    return result


class Search:
    def __init__(self, root, output, plan):
        self.root, self.output, self.plan = root, output, plan
        self.results = []
        self.lock = threading.Lock()
        self.stop = threading.Event()

    def check_frozen(self):
        for name, digest in self.plan['source_sha256'].items():
            assert sha(self.root / name) == digest, ('Source changed', name)
        assert sha(self.plan['binary']) == self.plan['binary_sha256']
        for name, digest in self.plan['input_sha256'].items():
            assert sha(name) == digest, ('Input changed', name)

    def trial(self, configuration, phase, number, requests, cpu):
        binary = self.output / f'binaries/worker-{cpu}/benchmark_potential_surface'
        binary.parent.mkdir(exist_ok=True)
        if not binary.exists():
            shutil.copy2(self.plan['binary'], binary)
        assert sha(binary) == self.plan['binary_sha256']
        destination = self.output / f'{phase}-{number:03d}'
        digest = sha(requests)
        command = ['taskset', '-c', str(cpu), str(binary), '--scene', self.plan['scene'],
            '--requests', str(requests), '--output', str(destination), '--repeats', '1', '--warmups', '0',
            '--weight', repr(configuration['weight']), '--support', repr(configuration['support']),
            '--length-scale', repr(configuration['length_scale']), '--exact-buildings',
            '--steering', 'tangent', '--guidance-mode', 'none', '--no-move-log', '--no-aerial']
        with benchmark_executable(binary) as executable:
            command[command.index(str(binary))] = str(executable)
            with (self.output / f'{phase}-{number:03d}.log').open('w') as log:
                subprocess.run(command, cwd=self.root, stdout=log, stderr=subprocess.STDOUT, check=True)
        assert binary.is_file() and sha(binary) == self.plan['binary_sha256']
        doc = json.loads((destination / 'summary.json').read_text())
        check_document(doc, configuration, digest)
        ratios = [math.dist(a['final'], a['goal']) / a['initial_distance_m'] for a in doc['agents']]
        summary = doc['summary']
        entry = {'phase': phase, 'number': number, 'configuration': configuration,
            'output': str(destination), 'requests_sha256': digest, 'population': len(doc['agents']),
            **summary['statuses'], 'remaining_distance_ratio_clipped_mean': statistics.mean(min(1., r) for r in ratios),
            'remaining_distance_ratio_mean': statistics.mean(ratios),
            'remaining_distance_ratio_median': statistics.median(ratios),
            'updates': summary['updates'], 'cpu_ms': summary['cpu_ms'],
            'query_stats': summary['query_stats'], 'native_validation_segments': summary['validation_segments'],
            'execution': {'original_executable': str(binary), 'during_run': str(executable),
                          'restored_after_run': True, 'command': command}}
        with self.lock:
            self.results.append(entry)
            write(self.output / 'results.json', sorted(self.results, key=lambda e: (e['phase'], e['number'])))
            print(f"{phase} {number:03d}: W={configuration['weight']:.5g}, L={configuration['length_scale']:.4g}, "
                  f"S={configuration['support']:.4g}: {entry['arrived']}/{entry['population']} arrived, "
                  f"{entry['stalled']} stalled, {entry['timeout']} timeout", flush=True)
        del doc
        return entry

    def parallel(self, configurations, phase, requests):
        jobs = queue.Queue()
        for number, configuration in enumerate(configurations):
            jobs.put((number, configuration))

        def worker(cpu):
            try:
                while not self.stop.is_set():
                    try:
                        number, configuration = jobs.get_nowait()
                    except queue.Empty:
                        return
                    self.trial(configuration, phase, number, requests, cpu)
            except BaseException:
                self.stop.set()
                raise

        try:
            with ThreadPoolExecutor(max_workers=len(self.plan['screen_cpus'])) as pool:
                futures = [pool.submit(worker, cpu) for cpu in self.plan['screen_cpus']]
                try:
                    for future in futures:
                        future.result()
                except BaseException:
                    self.stop.set()
                    raise
        except BaseException:
            self.stop.set()
            raise
        self.check_frozen()
        return sorted((e for e in self.results if e['phase'] == phase), key=lambda e: e['number'])


def check_document(doc, configuration, requests_digest):
    assert doc['native_validation_passed'] and doc['deterministic']
    assert doc['requests_sha256'] == requests_digest
    c, s = doc['configuration'], doc['summary']
    assert (c['weight'], c['length_scale_m'], c['support_m']) == (
        configuration['weight'], configuration['length_scale'], configuration['support'])
    assert c['exact_buildings'] and c['steering_mode'] == 'tangent' and c['guidance_mode'] == 'none'
    assert c['speed_m_per_second'] == 2 and c['timestep_seconds'] == .05
    assert (c['radius_m'], c['skin_m'], c['building_extra_m']) == (.3, .002, .05)
    assert not c['navmesh'] and not c['global_paths'] and not c['agent_agent_collisions']
    assert s['statuses']['invalid_endpoint'] == s['invalid_moves'] == s['validation_violations'] == 0


def freeze(args):
    root = Path(__file__).resolve().parents[1]
    output, previous = args.output.resolve(), args.recovery_run.resolve()
    output.mkdir(parents=True, exist_ok=False)
    old_plan = json.loads((previous / 'plan.json').read_text())
    scene = Path(old_plan['scene']).resolve()
    assert sha(scene) == old_plan['scene_sha256']
    binary = output / 'binaries/benchmark_potential_surface'
    binary.parent.mkdir(); shutil.copy2(args.binary.resolve(), binary)
    names = sorted(set(source_files(root) + ['tools/agent_benchmark_process.py',
        'tests/test_agent_benchmark_process.py']))
    hashes = {name: sha(root / name) for name in names}
    for name in names:
        target = output / 'source-snapshot' / name
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(root / name, target)
    requests = {}
    inputs = {str(scene): sha(scene), str(args.binary.resolve()): sha(args.binary.resolve()),
              str(previous / 'plan.json'): sha(previous / 'plan.json')}
    for cohort in ('training', 'second-seed'):
        reference = previous / f'{cohort}-rectangles-tangent/simulation/summary.json'
        inputs[str(reference)] = sha(reference)
        source = previous / f'{cohort}-requests.json'
        target = output / f'{cohort}-requests.json'
        shutil.copy2(source, target); assert sha(source) == sha(target)
        requests[cohort] = target
        inputs[str(source)] = sha(source); inputs[str(target)] = sha(target)
    original = json.loads(requests['training'].read_text())
    assert len(original['requests']) == 1000 and original['source_sha256'] == sha(scene)
    assert original['seed'] == SEEDS['training']
    indices = [i * 1000 // args.subset for i in range(args.subset)]
    subset = output / 'subset.json'
    write(subset, {**original, 'requests': [original['requests'][i] for i in indices]})
    inputs[str(subset)] = sha(subset)
    plan = {'schema': 1, 'created_at_utc': utc(), 'recovery_run': str(previous),
        'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=root, text=True).strip(),
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'scene': str(scene), 'scene_sha256': sha(scene), 'binary': str(binary), 'binary_sha256': sha(binary),
        'source_sha256': hashes, 'input_sha256': inputs, 'baseline': BASELINE, 'seeds': SEEDS,
        'subset_indices_zero_based': indices, 'subset_sha256': sha(subset),
        'screen_cpus': args.screen_cpus, 'final_cpu': args.cpu, 'final_repeats': 1, 'final_warmups': 0,
        'coarse': candidates(), 'full_finalists': args.finalists,
        'ranking': 'Most arrivals, then lower whole-cohort mean min(1, remaining/initial), then support, length and weight ascending. CPU never selects parameters.',
        'refinement': 'Best two positive-strength centers separated by summed absolute log2 parameter distance >=2. Test kappa x2,/2, L and support x sqrt(2),/sqrt(2), and S48 when a center has S24; bounds kappa[.25,4096],L[.5,8],S[1.5,48].',
        'selection': 'Top three distinct subset candidates except inherited baseline, including W0 if ranked, evaluated on full1000 training. Inherited baseline remains eligible. Lock before fresh holdout generation.',
        'fixed_controller': 'Exact building rectangles, capsule fence fields, circular tree fields, normalized filtering and existing persistent tangent; no hierarchy/pockets or new steering logic.',
        'fixed_physics': {'radius_m': .3, 'skin_m': .002, 'building_extra_m': .05,
                          'speed_m_per_second': 2, 'timestep_seconds': .05, 'arrival_radius_m': .15},
        'validation': 'Training and second-seed reused. Fresh seed1515146564 generated only after winner lock; no component filtering. Screening/fullselection native checks only; final six full movement stream audits.',
        'naming_protocol': 'Each benchmark copy is renamed to a basename containing agent and no zombie/zombies before execution and restored afterward, including exceptions.',
        'timing_scope': 'Screening runs concurrently on distinct CPU affinities and timing never ranks. Six final cases run sequentially on one CPU; matched first100 all-active ticks are exploratory single-pass observations.'}
    write(output / 'plan.json', plan)
    (output / 'source.diff').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=root))
    return root, output, previous, plan, requests, subset


def save_measurements(output, results):
    fields = ['phase', 'number', 'population', 'weight', 'length_scale', 'support', 'boundary_strength',
              'arrived', 'stalled', 'timeout', 'remaining_distance_ratio_clipped_mean',
              'remaining_distance_ratio_mean', 'remaining_distance_ratio_median', 'updates', 'cpu_ms']
    with (output / 'screening.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore', lineterminator='\n')
        writer.writeheader()
        writer.writerows({**e, **e['configuration']} for e in sorted(results, key=lambda e: (e['phase'], e['number'])))


def final_runs(root, output, plan, requests, winner):
    winner_path = output / 'winner.json'
    winner_digest = sha(winner_path)
    holdout = output / 'holdout-requests.json'
    subprocess.run([sys.executable, str(root / 'tools/prepare_potential_surface_requests.py'),
        '--scene', plan['scene'], '--output', str(holdout), '--population', '1000',
        '--seed', str(SEEDS['holdout'])], cwd=root, check=True)
    assert sha(winner_path) == winner_digest
    requests = {**requests, 'holdout': holdout}
    final_inputs = {'locked_winner_sha256': winner_digest,
        'winner_locked_at_utc': winner['locked_at_utc'], 'holdout_generated_at_utc': utc(),
        'requests': {name: {'file': str(path), 'sha256': sha(path), 'seed': SEEDS[name]}
                     for name, path in requests.items()}, 'cases': []}
    for cohort in ('training', 'second-seed', 'holdout'):
        for variant, config in (('baseline', BASELINE), ('tuned', winner['configuration'])):
            name = f'{cohort}-{variant}'
            destination = output / 'final' / name
            command = [sys.executable, str(root / 'tools/run_potential_surface_experiment.py'),
                '--binary', plan['binary'], '--scene', plan['scene'], '--requests', str(requests[cohort]),
                '--output', str(destination), '--population', '1000', '--repeats', '1', '--warmups', '0',
                '--cpu', str(plan['final_cpu']), '--weight', repr(config['weight']),
                '--length-scale', repr(config['length_scale']), '--support', repr(config['support']),
                '--steering', 'tangent', '--exact-buildings', '--guidance-mode', 'none']
            final_inputs['cases'].append({'cohort': cohort, 'variant': variant, 'name': name,
                'output': str(destination), 'requests_sha256': sha(requests[cohort]),
                'configuration': config, 'command': command})
    write(output / 'final-inputs.json', final_inputs)
    (output / 'final').mkdir()
    for number, case in enumerate(final_inputs['cases'], 1):
        print(f"FINAL {number}/6: {case['name']}", flush=True)
        with (output / (case['name'] + '.log')).open('w') as log:
            subprocess.run(case['command'], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        doc = json.loads((Path(case['output']) / 'simulation/summary.json').read_text())
        check_document(doc, case['configuration'], case['requests_sha256'])
        if case['variant'] == 'baseline' and case['cohort'] != 'holdout':
            reference = Path(plan['recovery_run']) / f"{case['cohort']}-rectangles-tangent/simulation/summary.json"
            old = json.loads(reference.read_text())
            assert retained_values_equal(old['agents'], doc['agents']), 'Inherited baseline changed'
            assert retained_values_equal(old['summary']['query_stats'], doc['summary']['query_stats'])
            del old
        print(case['name'], doc['summary']['statuses'], flush=True)
        assert sha(winner_path) == winner_digest
        for request in final_inputs['requests'].values():
            assert sha(request['file']) == request['sha256']
        del doc
    from compare_potential_surface_retuning import compare
    compare(output)


def run(args):
    root, output, previous, plan, requests, subset = freeze(args)
    search = Search(root, output, plan)
    baseline = search.trial(BASELINE, 'compatibility', 0, requests['training'], plan['screen_cpus'][0])
    reference = json.loads((previous / 'training-rectangles-tangent/simulation/summary.json').read_text())
    new = json.loads((Path(baseline['output']) / 'summary.json').read_text())
    assert retained_values_equal(reference['agents'], new['agents'])
    assert retained_values_equal(reference['summary']['query_stats'], new['summary']['query_stats'])
    del reference, new
    print('Current controller exactly reproduces retained full training baseline.', flush=True)
    coarse = search.parallel(plan['coarse'], 'coarse', subset)
    centers = []
    for entry in sorted((e for e in coarse if e['configuration']['boundary_strength'] > 0), key=rank):
        c = entry['configuration']
        if all(sum(abs(math.log2(c[n] / other[n])) for n in ('boundary_strength', 'length_scale', 'support')) >= 2
               for other in centers):
            centers.append(c)
        if len(centers) == 2:
            break
    refined = refinements(centers, plan['coarse'])
    write(output / 'refinement-plan.json', {'centers': centers, 'configurations': refined})
    refinement_results = search.parallel(refined, 'refine', subset)
    candidates_ranked = sorted((e for e in coarse + refinement_results
                                if key(e['configuration']) != key(BASELINE)), key=rank)
    finalists = candidates_ranked[:args.finalists]
    write(output / 'finalists.json', finalists)
    full = search.parallel([e['configuration'] for e in finalists], 'full', requests['training'])
    chosen = sorted(full + [baseline], key=rank)[0]
    winner = {'schema': 1, 'locked_at_utc': utc(), 'holdout_seed': SEEDS['holdout'],
              'configuration': chosen['configuration'], 'selection_result': chosen,
              'holdout_not_yet_generated': True}
    write(output / 'winner.json', winner)
    save_measurements(output, search.results)
    search.check_frozen()
    print('LOCKED WINNER ' + json.dumps(winner['configuration']), flush=True)
    final_runs(root, output, plan, requests, winner)
    search.check_frozen()
    print('COMPLETE ' + str(output / 'comparison.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--recovery-run', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--subset', type=int, default=128)
    parser.add_argument('--finalists', type=int, default=3)
    parser.add_argument('--screen-cpus', nargs='+', type=int, default=[2, 3, 4, 5])
    parser.add_argument('--cpu', type=int, default=2)
    arguments = parser.parse_args()
    if not (1 <= arguments.subset <= 1000 and 1 <= arguments.finalists <= 10):
        parser.error('Require subset1..1000 and finalists1..10')
    if len(set(arguments.screen_cpus)) != len(arguments.screen_cpus):
        parser.error('Screen CPU affinities must be distinct')
    run(arguments)
