#!/usr/bin/env python3
"""Freeze and run the goal-directed Potential Surface Steering map experiment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def run(args):
    root = Path(__file__).resolve().parents[1]
    output, scene = args.output.resolve(), args.scene.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = output / 'binaries/benchmark_potential_surface'
    binary.parent.mkdir()
    shutil.copy2(args.binary.resolve(), binary)
    sources = ['experiments/potential_surface/CMakeLists.txt',
               'src/file_fingerprint.cpp', 'src/file_fingerprint.hpp']
    for directory, pattern in [('src', 'potential_surface*'), ('tools', '*potential_surface*'),
                               ('tests', 'test_potential_surface*')]:
        sources += [str(p.relative_to(root)) for p in sorted((root / directory).glob(pattern))
                    if p.is_file()]
    for name in sources:
        target = output / 'source-snapshot' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / name, target)
    raster = json.loads(scene.read_text())['raster']
    aerial = scene.parent / raster['file']
    manifest = {
        'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=root, text=True).strip(),
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'source_sha256': {name: sha(root / name) for name in sources},
        'scene': str(scene), 'scene_sha256': sha(scene), 'aerial_sha256': sha(aerial),
        'binary_sha256': sha(binary), 'cpu_affinity': args.cpu,
        'population': args.population, 'seed': args.seed, 'repeats': args.repeats,
        'warmups': args.warmups, 'compiler': subprocess.check_output(['c++', '--version'], text=True).splitlines()[0],
        'build': 'Release -O3 -DNDEBUG', 'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'source.diff').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=root))
    environment = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'}
    requests = output / 'requests.json'
    subprocess.run([sys.executable, str(root / 'tools/prepare_potential_surface_requests.py'),
                    '--scene', str(scene), '--output', str(requests),
                    '--population', str(args.population), '--seed', str(args.seed)],
                   cwd=root, env=environment, check=True)
    manifest['requests_sha256'] = sha(requests)
    command = ['taskset', '-c', str(args.cpu), str(binary), '--scene', str(scene),
               '--requests', str(requests), '--output', str(output / 'simulation'),
               '--repeats', str(args.repeats), '--warmups', str(args.warmups),
               '--timestep', '.05', '--speed', '2']
    manifest['command'] = command
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Running frozen goal-directed steering benchmark...', flush=True)
    with (output / 'simulation.log').open('w') as log:
        subprocess.run(['/usr/bin/time', '-o', str(output / 'process.json'), '-f',
                        '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"peak_rss_kib":%M}',
                        *command], cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    for name, digest in manifest['source_sha256'].items():
        assert sha(root / name) == digest, ('Source changed during measurement', name)
    assert sha(binary) == manifest['binary_sha256']
    assert sha(scene) == manifest['scene_sha256'] and sha(aerial) == manifest['aerial_sha256']
    assert sha(requests) == manifest['requests_sha256']
    summary = output / 'simulation/summary.json'
    result = json.loads(summary.read_text())
    assert result['native_validation_passed'] and result['deterministic']
    assert result['summary']['statuses']['invalid_endpoint'] == 0
    assert result['summary']['population'] == args.population
    assert len(result['samples']) == args.repeats
    assert not result['configuration']['navmesh'] and not result['configuration']['global_paths']
    print('Simulation complete; independently auditing every recorded movement...', flush=True)
    subprocess.run([sys.executable, str(root / 'tools/audit_potential_surface.py'),
                    '--scene', str(scene), '--summary', str(summary),
                    '--output', str(output / 'audit.json')], cwd=root, env=environment, check=True)
    subprocess.run([sys.executable, str(root / 'tools/render_potential_surface.py'),
                    '--summary', str(summary), '--output', str(output / 'potential-surface-paths.png')],
                   cwd=root, env=environment, check=True)
    for name, digest in manifest['source_sha256'].items():
        assert sha(root / name) == digest, ('Source changed during experiment', name)
    print(output, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--population', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=1515146562)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--warmups', type=int, default=1)
    parser.add_argument('--cpu', type=int, default=2)
    run(parser.parse_args())
