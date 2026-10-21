#!/usr/bin/env python3
"""Freeze and benchmark four incremental PSS variants on two existing trip sets."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

from compare_potential_surface_recovery import VARIANTS, compare
from run_potential_surface_experiment import sha


def run(args):
    root = Path(__file__).resolve().parents[1]
    previous, output = args.tuning_run.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    winner_file = previous/'winner.json'
    winner = json.loads(winner_file.read_text())
    old_manifest = json.loads((previous/'manifest.json').read_text())
    scene = Path(old_manifest['scene'])
    assert sha(scene) == old_manifest['scene_sha256']
    binary = output/'binaries/steering'
    binary.parent.mkdir(); shutil.copy2(args.binary.resolve(), binary)
    parameters = winner['configuration']
    cases = []
    for cohort, request_file, reference_name in (
            ('training', previous/'full-training-requests.json', 'training-tuned'),
            ('second-seed', previous/'holdout-requests.json', 'holdout-tuned')):
        copied = output/f'{cohort}-requests.json'
        shutil.copy2(request_file, copied)
        expected_hash = sha(request_file)
        assert sha(copied) == expected_hash
        for variant, exact, mode in VARIANTS:
            name = f'{cohort}-{variant}'
            command = [sys.executable, str(root/'tools/run_potential_surface_experiment.py'),
                '--binary', str(binary), '--scene', str(scene), '--requests', str(copied),
                '--output', str(output/name), '--population', '1000', '--repeats', '1', '--warmups', '0',
                '--cpu', str(args.cpu), '--weight', repr(parameters['weight']),
                '--length-scale', repr(parameters['length_scale']), '--support', repr(parameters['support']),
                '--steering', mode]
            if exact: command.append('--exact-buildings')
            cases.append({'name': name, 'variant': variant, 'command': command,
                'requests_sha256': expected_hash, 'original_requests': str(request_file),
                'reference': str(previous/f'final/{reference_name}/simulation/summary.json')})
    sources = ['experiments/potential_surface/CMakeLists.txt', 'src/file_fingerprint.cpp', 'src/file_fingerprint.hpp']
    for directory, pattern in [('src', 'potential_surface*'), ('tools', '*potential_surface*'),
                               ('tests', 'test_potential_surface*')]:
        sources += [str(p.relative_to(root)) for p in sorted((root/directory).glob(pattern)) if p.is_file()]
    hashes = {name: sha(root/name) for name in sources}
    for name in sources:
        target = output/'source-snapshot'/name
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(root/name, target)
    plan = {'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'parameters': parameters, 'scene': str(scene), 'scene_sha256': sha(scene),
        'winner_sha256': sha(winner_file), 'binary_sha256': sha(binary),
        'source_sha256': hashes, 'cpu': args.cpu, 'cases': cases,
        'repeats': 1, 'warmups': 0,
        'timing_scope': 'Exploratory single-pass observations on identical common first-five-second all-active ticks; no confidence or statistical speed ranking.',
        'selection': 'Four predeclared sequential ablations; no parameter tuning against either trip set.',
        'validation_scope': 'Both trip sets were used in the prior study; second-seed is a repeated workload, not a pristine holdout.'}
    (output/'plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    for number, case in enumerate(cases, 1):
        print(f"Case {number}/{len(cases)}: {case['name']}", flush=True)
        with (output/f"{case['name']}.log").open('w') as log:
            subprocess.run(case['command'], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        document = json.loads((output/case['name']/'simulation/summary.json').read_text())
        print(case['name'], document['summary']['statuses'], flush=True)
        assert document['requests_sha256'] == case['requests_sha256'] == sha(case['original_requests'])
        if case['variant'] == 'circles-filtered':
            reference = json.loads(Path(case['reference']).read_text())
            for old, new in zip(reference['agents'], document['agents']):
                assert {k: new[k] for k in old} == old, 'Circle baseline movement changed'
            assert len(reference['agents']) == len(document['agents']) == 1000
            assert reference['summary']['query_stats'] == document['summary']['query_stats']
            print('Circle baseline reproduces retained agent records and query counts exactly.', flush=True)
        for name, digest in hashes.items(): assert sha(root/name) == digest, name
        assert sha(binary) == plan['binary_sha256'] and sha(scene) == plan['scene_sha256']
        assert sha(winner_file) == plan['winner_sha256']
    compare(output)
    print(output/'comparison.json', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--tuning-run', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=2)
    run(parser.parse_args())
