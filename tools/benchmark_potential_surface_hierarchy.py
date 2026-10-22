#!/usr/bin/env python3
"""Freeze four guidance ablations on both retained 1,000-trip PSS workloads."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

from compare_potential_surface_hierarchy import VARIANTS, compare
from run_potential_surface_experiment import sha


def retained_values_equal(old, new):
    """Allow new diagnostic fields while checking every retained value exactly."""
    if old == new:
        return True
    if isinstance(old, dict):
        return isinstance(new, dict) and all(k in new and retained_values_equal(v, new[k])
                                              for k, v in old.items())
    if isinstance(old, list):
        return (isinstance(new, list) and len(old) == len(new) and
                all(retained_values_equal(a, b) for a, b in zip(old, new)))
    return old == new


def source_files(root):
    names = ['experiments/potential_surface/CMakeLists.txt',
             'src/file_fingerprint.cpp', 'src/file_fingerprint.hpp']
    for directory, pattern in [('src', 'potential_surface*'), ('tools', '*potential_surface*'),
                               ('tests', 'test_potential_surface*')]:
        names.extend(str(p.relative_to(root)) for p in sorted((root/directory).glob(pattern))
                     if p.is_file())
    return sorted(set(names))


def run(args):
    root = Path(__file__).resolve().parents[1]
    previous, output = args.recovery_run.resolve(), args.output.resolve()
    old_plan_path = previous/'plan.json'
    old_plan = json.loads(old_plan_path.read_text())
    scene = Path(old_plan['scene']).resolve()
    parameters = old_plan['parameters']
    assert (parameters['weight'], parameters['length_scale'], parameters['support']) == (
        1448.1546878700494, 2, 12), 'This comparison preserves the previously selected parameters'
    assert sha(scene) == old_plan['scene_sha256']
    guidance_source = args.guidance.resolve()
    guidance_digest = sha(guidance_source)
    baked = json.loads(guidance_source.read_text())
    assert baked['schema'] == 1 and baked['source_sha256'] == sha(scene)
    assert baked['physical_padding'] == {'radius_m': .3, 'skin_m': .002, 'building_extra_m': .05}
    reachability_source = previous/'reachability/classified/comparison.json'
    reachability_document = json.loads(reachability_source.read_text())
    assert reachability_document['source_sha256'] == sha(scene)
    output.mkdir(parents=True, exist_ok=False)
    binary = output/'binaries/steering'
    binary.parent.mkdir()
    shutil.copy2(args.binary.resolve(), binary)
    guidance = output/'guidance.json'
    shutil.copy2(guidance_source, guidance)
    assert sha(guidance) == guidance_digest
    reachability = output/'reference-reachability.json'
    shutil.copy2(reachability_source, reachability)
    input_hashes = {str(scene): sha(scene), str(old_plan_path): sha(old_plan_path),
                    str(guidance_source): guidance_digest,
                    str(reachability_source): sha(reachability_source)}
    cases = []
    for cohort in ('training', 'second-seed'):
        retained_case = previous/f'{cohort}-rectangles-tangent'
        request_source = retained_case/'requests.json'
        reference = retained_case/'simulation/summary.json'
        request_digest = sha(request_source)
        input_hashes[str(request_source)] = request_digest
        input_hashes[str(reference)] = sha(reference)
        requests = output/f'{cohort}-requests.json'
        shutil.copy2(request_source, requests)
        assert sha(requests) == request_digest
        for variant, mode in VARIANTS:
            name = f'{cohort}-{variant}'
            command = [sys.executable, str(root/'tools/run_potential_surface_experiment.py'),
                '--binary', str(binary), '--scene', str(scene), '--requests', str(requests),
                '--output', str(output/name), '--population', '1000', '--repeats', '1', '--warmups', '0',
                '--cpu', str(args.cpu), '--weight', repr(parameters['weight']),
                '--length-scale', repr(parameters['length_scale']), '--support', repr(parameters['support']),
                '--steering', 'tangent', '--exact-buildings', '--guidance-mode', mode]
            if mode != 'none':
                command.extend(['--guidance', str(guidance)])
            cases.append({'name': name, 'cohort': cohort, 'variant': variant, 'guidance_mode': mode,
                          'command': command, 'requests_sha256': request_digest,
                          'requests': str(requests), 'reference': str(reference)})
    sources = source_files(root)
    hashes = {name: sha(root/name) for name in sources}
    for name in sources:
        target = output/'source-snapshot'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/name, target)
    plan = {
        'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=root, text=True).strip(),
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'recovery_run': str(previous), 'parameters': parameters,
        'scene': str(scene), 'scene_sha256': sha(scene), 'binary_sha256': sha(binary),
        'source_sha256': hashes, 'input_sha256': input_hashes, 'cpu': args.cpu,
        'guidance': {'file': str(guidance), 'original': str(guidance_source), 'sha256': guidance_digest,
                     'schema': baked['schema'], 'source_sha256': baked['source_sha256'],
                     'physical_padding': baked['physical_padding'], 'parameters': baked.get('parameters', {}),
                     'diagnostics': baked.get('diagnostics', {}),
                     'bumps': len(baked.get('bumps', [])), 'pockets': len(baked.get('pockets', []))},
        'reachability': {'file': str(reachability), 'sha256': sha(reachability),
                         'interpretation': 'Prior model-specific impossible, witnessed/recorded reachable, and unresolved strata; no requests are filtered.'},
        'cases': cases, 'repeats': 1, 'warmups': 0,
        'timing_scope': 'Exploratory single-pass observations on matched first-five-second all-active ticks; no confidence bounds or universal speed ranking.',
        'selection': 'Four predeclared guidance ablations at unchanged physical and steering parameters; no parameter search on either trip set.',
        'validation_scope': 'Both workloads were used in earlier studies; the second seed is a repeated workload, not a pristine holdout.',
    }
    (output/'plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    (output/'source.diff').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=root))
    for number, case in enumerate(cases, 1):
        print(f"Case {number}/{len(cases)}: {case['name']}", flush=True)
        with (output/f"{case['name']}.log").open('w') as log:
            subprocess.run(case['command'], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        document = json.loads((output/case['name']/'simulation/summary.json').read_text())
        assert document['requests_sha256'] == case['requests_sha256'] == sha(case['requests'])
        if case['variant'] == 'baseline':
            reference = json.loads(Path(case['reference']).read_text())
            assert retained_values_equal(reference['agents'], document['agents']), 'Baseline movement changed'
            assert retained_values_equal(reference['summary']['query_stats'], document['summary']['query_stats'])
            assert len(reference['agents']) == len(document['agents']) == 1000
            print('Baseline reproduces every retained agent value and query counter exactly.', flush=True)
            del reference
        print(case['name'], document['summary']['statuses'], flush=True)
        for name, digest in hashes.items():
            assert sha(root/name) == digest, ('Source changed during comparison', name)
        for name, digest in input_hashes.items():
            assert sha(name) == digest, ('Input changed during comparison', name)
        assert sha(binary) == plan['binary_sha256']
        assert sha(guidance) == guidance_digest and sha(reachability) == plan['reachability']['sha256']
        del document
    compare(output)
    from render_potential_surface_hierarchy import render
    for cohort in ('training', 'second-seed'):
        render(output, cohort)
    print(output/'comparison.json', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--recovery-run', type=Path, required=True)
    parser.add_argument('--guidance', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=2)
    run(parser.parse_args())
