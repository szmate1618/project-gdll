#!/usr/bin/env python3
"""Screen PSS parameters on fixed training trips, then lock a full-set winner."""
import argparse
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def key(config):
    return tuple(round(config[name], 10) for name in ('weight', 'support', 'length_scale'))


def parameters(strength, length, support):
    return {'weight': strength * length * length, 'length_scale': length,
            'support': support, 'boundary_strength': strength}


def rank(entry):
    # A deterministic ranking, independent of noisy one-pass timing.
    return (-entry['arrived'], entry['remaining_distance_ratio_median'],
            entry['configuration']['support'], entry['configuration']['length_scale'],
            entry['configuration']['weight'])


def freeze(root, output, binary):
    (output / 'binaries').mkdir(parents=True)
    target = output / 'binaries/steering'
    shutil.copy2(binary, target)
    names = ['experiments/potential_surface/CMakeLists.txt',
             'src/file_fingerprint.cpp', 'src/file_fingerprint.hpp']
    for directory, pattern in [('src', 'potential_surface*'), ('tools', '*potential_surface*'),
                               ('tests', 'test_potential_surface*')]:
        names += [str(p.relative_to(root)) for p in sorted((root/directory).glob(pattern)) if p.is_file()]
    hashes = {name: sha(root/name) for name in names}
    for name in names:
        destination = output/'source-snapshot'/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/name, destination)
    return target, hashes


def run(args):
    root = Path(__file__).resolve().parents[1]
    output, scene, source = args.output.resolve(), args.scene.resolve(), args.requests.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary, hashes = freeze(root, output, args.binary.resolve())
    original = json.loads(source.read_text())
    assert original['source_sha256'] == sha(scene)
    count = len(original['requests'])
    assert 1 <= args.subset <= count
    indices = [i*count//args.subset for i in range(args.subset)]
    training = {**original, 'requests': [original['requests'][i] for i in indices]}
    subset = output/'subset.json'
    subset.write_text(json.dumps(training, indent=2)+'\n')
    copied_source = output/'full-training-requests.json'
    shutil.copy2(source, copied_source)
    original_hash = sha(source)
    assert sha(copied_source) == original_hash
    coarse = [parameters(k, length, support) for k, length, support in itertools.product(
        [.25, 1, 4, 16, 64, 256], [.5, 1, 2], [1.5, 3, 6, 12])]
    coarse.insert(0, parameters(0, 1, 3))
    manifest = {
        'base_commit': subprocess.check_output(['git','rev-parse','HEAD'], cwd=root, text=True).strip(),
        'branch': subprocess.check_output(['git','branch','--show-current'], cwd=root, text=True).strip(),
        'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'binary_sha256': sha(binary), 'source_sha256': hashes,
        'scene': str(scene), 'scene_sha256': sha(scene),
        'original_requests': str(source), 'original_requests_sha256': original_hash,
        'copied_full_requests_sha256': original_hash,
        'subset_indices_zero_based': indices, 'subset_sha256': sha(subset),
        'coarse': coarse, 'holdout_seed': 1515146563, 'cpu_affinity': args.cpu,
        'selection': 'Most arrivals; then lowest whole-cohort median remaining/initial distance; then support, length scale, weight ascending. Timing does not select the winner.',
        'refinement': 'Six geometric axis neighbors (factor sqrt(2)) around each of the best two distinct positive-strength coarse settings; deduplicate existing configurations. Also test support*2 for each center, capped at 24m.',
        'full_screen': 'Top three unique positive-strength configurations on the full original requests; the compatibility baseline also remains eligible. Freeze winning parameters before generating or examining holdout results.',
    }
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (output/'source.diff').write_bytes(subprocess.check_output(['git','diff','HEAD','--binary'], cwd=root))
    results = []

    def trial(config, phase, requests, number):
        expected_requests_hash = (manifest['copied_full_requests_sha256'] if requests==copied_source
                                  else manifest['subset_sha256'])
        assert sha(requests) == expected_requests_hash
        destination = output/f'{phase}-{number:03d}'
        command = ['taskset','-c',str(args.cpu),str(binary),'--scene',str(scene),
            '--requests',str(requests),'--output',str(destination),'--repeats','1','--warmups','0',
            '--weight',repr(config['weight']),'--support',repr(config['support']),
            '--length-scale',repr(config['length_scale']),'--no-move-log','--no-aerial']
        with (output/f'{phase}-{number:03d}.log').open('w') as log:
            subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        document = json.loads((destination/'summary.json').read_text())
        assert document['native_validation_passed'] and document['deterministic']
        assert document['requests_sha256'] == expected_requests_hash == sha(requests)
        actual = document['configuration']
        assert (actual['weight'], actual['support_m'], actual['length_scale_m']) == (
            config['weight'], config['support'], config['length_scale'])
        assert actual['speed_m_per_second'] == 2 and actual['timestep_seconds'] == .05
        assert actual['radius_m'] == .3 and actual['skin_m'] == .002 and actual['building_extra_m'] == .05
        assert not actual['navmesh'] and not actual['global_paths'] and not actual['agent_agent_collisions']
        summary = document['summary']
        assert summary['statuses']['invalid_endpoint'] == summary['invalid_moves'] == summary['validation_violations'] == 0
        ratio = [math.dist(a['final'], a['goal'])/a['initial_distance_m'] for a in document['agents']]
        entry = {'phase': phase, 'number': number, 'configuration': config,
                 'output': str(destination), 'population': summary['population'], **summary['statuses'],
                 'remaining_distance_ratio_median': statistics.median(ratio),
                 'updates': summary['updates'], 'cpu_ms': summary['cpu_ms'],
                 'first_5s_all_active_step_cpu_ms': summary['first_5s_all_active_step_cpu_ms'],
                 'summary': summary}
        results.append(entry)
        (output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
        print(f"{phase} {number:03d}: k={config['boundary_strength']:g}, L={config['length_scale']:g}, F={config['support']:g}: "
              f"{entry['arrived']}/{entry['population']} arrived, {entry['stalled']} stalled, {entry['timeout']} timeout", flush=True)
        return document, entry

    # Verify the exposed defaults reproduce every retained baseline agent.
    baseline = parameters(16, 1, 3)
    document, _ = trial(baseline, 'compatibility', copied_source, 0)
    if args.baseline_summary:
        retained = json.loads(args.baseline_summary.read_text())
        assert len(document['agents']) == len(retained['agents'])
        for old, new in zip(retained['agents'], document['agents']):
            assert {name: new[name] for name in old} == old, 'Default behavior changed from the committed experiment'
        assert document['summary']['query_stats'] == retained['summary']['query_stats']
        print('Default parameters exactly reproduce every retained baseline agent and query count.', flush=True)
    for number, config in enumerate(coarse):
        trial(config, 'coarse', subset, number)
    seen = {key(config) for config in coarse}
    centers = []
    for candidate in sorted((e for e in results if e['phase']=='coarse' and e['configuration']['weight']>0), key=rank):
        config = candidate['configuration']
        if all(sum(abs(math.log2(config[name]/other[name])) for name in ('boundary_strength','length_scale','support'))>=2
               for other in centers):
            centers.append(config)
        if len(centers)==2:
            break
    refined = []
    for center in centers:
        for name in ('boundary_strength','length_scale','support'):
            for multiplier in (1/math.sqrt(2), math.sqrt(2)):
                values = dict(center);values[name]*=multiplier
                config = parameters(values['boundary_strength'], values['length_scale'], values['support'])
                if key(config) not in seen:
                    seen.add(key(config));refined.append(config)
        if center['support']<24:
            config=parameters(center['boundary_strength'],center['length_scale'],min(24,center['support']*2))
            if key(config) not in seen:
                seen.add(key(config));refined.append(config)
    (output/'refinement-plan.json').write_text(json.dumps({'centers':centers,'configurations':refined}, indent=2)+'\n')
    for number, config in enumerate(refined):
        trial(config, 'refine', subset, number)
    finalists = sorted((e for e in results if e['phase'] in ('coarse','refine') and e['configuration']['weight']>0), key=rank)[:3]
    (output/'finalists.json').write_text(json.dumps(finalists, indent=2)+'\n')
    full = []
    for number, entry in enumerate(finalists):
        _, result = trial(entry['configuration'], 'full', copied_source, number)
        full.append(result)
    compatibility = next(entry for entry in results if entry['phase']=='compatibility')
    winner = sorted(full+[compatibility], key=rank)[0]
    (output/'winner.json').write_text(json.dumps({'selection_locked_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
        'holdout_seed':1515146563,'configuration':winner['configuration'],'selection_result':winner,
        'holdout_not_yet_evaluated':True}, indent=2)+'\n')
    fields=['phase','number','population','weight','length_scale','support','boundary_strength','arrived','stalled','timeout',
            'remaining_distance_ratio_median','updates','cpu_ms']
    with (output/'measurements.csv').open('w', newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=fields,extrasaction='ignore',lineterminator='\n')
        writer.writeheader();writer.writerows({**e,**e['configuration']} for e in results)
    for name, digest in hashes.items():
        assert sha(root/name)==digest, ('Source changed during screening',name)
    assert sha(binary)==manifest['binary_sha256'] and sha(source)==manifest['original_requests_sha256']
    assert sha(copied_source)==manifest['copied_full_requests_sha256']
    assert sha(scene)==manifest['scene_sha256'] and sha(subset)==manifest['subset_sha256']
    print('LOCKED WINNER '+json.dumps(winner['configuration']), flush=True)
    print(output/'winner.json', flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--scene',type=Path,required=True)
    parser.add_argument('--requests',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--baseline-summary',type=Path)
    parser.add_argument('--subset',type=int,default=128)
    parser.add_argument('--cpu',type=int,default=2)
    run(parser.parse_args())
