#!/usr/bin/env python3
"""Freeze and run the goal-directed Potential Surface Steering map experiment."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from agent_benchmark_process import benchmark_executable


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def nonnegative_float(text):
    value = float(text)
    if not math.isfinite(value) or value < 0:
        raise argparse.ArgumentTypeError('must be finite and nonnegative')
    return value


def positive_float(text):
    value = nonnegative_float(text)
    if value == 0:
        raise argparse.ArgumentTypeError('must be strictly positive')
    return value


def run(args):
    root = Path(__file__).resolve().parents[1]
    output, scene = args.output.resolve(), args.scene.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = output / 'binaries/benchmark_potential_surface'
    binary.parent.mkdir()
    shutil.copy2(args.binary.resolve(), binary)
    sources = ['experiments/potential_surface/CMakeLists.txt',
               'tools/agent_benchmark_process.py', 'tests/test_agent_benchmark_process.py',
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
        'warmups': args.warmups, 'field_parameters': {
            'weight': args.weight, 'support_m': args.support, 'length_scale_m': args.length_scale,
            'exact_buildings': args.exact_buildings},
        'steering_mode': args.steering,
        'guidance_mode': args.guidance_mode,
        'record_moves': not args.no_move_log, 'copy_aerial': not args.no_aerial,
        'compiler': subprocess.check_output(['c++', '--version'], text=True).splitlines()[0],
        'build': 'Release -O3 -DNDEBUG', 'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'source.diff').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=root))
    environment = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'}
    requests = output / 'requests.json'
    original_requests = args.requests.resolve() if args.requests else None
    if original_requests is not None:
        original_requests_sha = sha(original_requests)
        prepared = json.loads(original_requests.read_text())
        if not isinstance(prepared.get('source'), str) or not prepared['source']:
            raise ValueError('Existing requests must record their source scene')
        if prepared.get('source_sha256') != manifest['scene_sha256']:
            raise ValueError('Existing requests were prepared against different scene bytes')
        if not isinstance(prepared.get('requests'), list) or len(prepared['requests']) != args.population:
            raise ValueError('Existing request population differs from --population')
        if sha(original_requests) != original_requests_sha:
            raise RuntimeError('Existing requests changed while being inspected')
        shutil.copy2(original_requests, requests)
        if sha(requests) != original_requests_sha:
            raise RuntimeError('Existing requests were not copied byte for byte')
        manifest.update(requests_reused=True, requests_original=str(original_requests),
                        requests_original_sha256=original_requests_sha,
                        requests_recorded_source=prepared['source'], seed=prepared.get('seed'))
    else:
        subprocess.run([sys.executable, str(root / 'tools/prepare_potential_surface_requests.py'),
                        '--scene', str(scene), '--output', str(requests),
                        '--population', str(args.population), '--seed', str(args.seed)],
                       cwd=root, env=environment, check=True)
        manifest['requests_reused'] = False
    manifest['requests_sha256'] = sha(requests)
    command = ['taskset', '-c', str(args.cpu), str(binary), '--scene', str(scene),
               '--requests', str(requests), '--output', str(output / 'simulation'),
               '--repeats', str(args.repeats), '--warmups', str(args.warmups),
               '--timestep', '.05', '--speed', '2',
               '--weight', str(args.weight), '--support', str(args.support),
               '--length-scale', str(args.length_scale), '--steering', args.steering]
    if args.exact_buildings:
        command.append('--exact-buildings')
    if args.guidance_mode != 'none':
        if args.guidance is None:
            raise ValueError('--guidance is required for an enabled guidance mode')
        guidance_source = args.guidance.resolve()
        guidance_copy = output / 'guidance.json'
        shutil.copy2(guidance_source, guidance_copy)
        manifest['guidance_original'] = str(guidance_source)
        manifest['guidance_sha256'] = sha(guidance_copy)
        command += ['--guidance', str(guidance_copy), '--guidance-mode', args.guidance_mode]
    elif args.guidance is not None:
        raise ValueError('--guidance requires an enabled --guidance-mode')
    if args.no_move_log:
        command.append('--no-move-log')
    if args.no_aerial:
        command.append('--no-aerial')
    print('Running frozen goal-directed steering benchmark...', flush=True)
    try:
        with benchmark_executable(binary) as running_binary:
            command[command.index(str(binary))] = str(running_binary)
            manifest['command'] = command
            manifest['benchmark_executable'] = {'original': str(binary),
                'during_run': str(running_binary), 'restored_after_run': False}
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
            with (output / 'simulation.log').open('w') as log:
                subprocess.run(['/usr/bin/time', '-o', str(output / 'process.json'), '-f',
                                '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"peak_rss_kib":%M}',
                                *command], cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    finally:
        if 'benchmark_executable' in manifest:
            temporary = Path(manifest['benchmark_executable']['during_run'])
            manifest['benchmark_executable']['restored_after_run'] = (
                binary.is_file() and sha(binary) == manifest['binary_sha256']
                and not temporary.exists() and not temporary.is_symlink())
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    for name, digest in manifest['source_sha256'].items():
        assert sha(root / name) == digest, ('Source changed during measurement', name)
    assert sha(binary) == manifest['binary_sha256']
    assert sha(scene) == manifest['scene_sha256'] and sha(aerial) == manifest['aerial_sha256']
    assert sha(requests) == manifest['requests_sha256']
    if original_requests is not None:
        assert sha(original_requests) == manifest['requests_original_sha256']
    summary = output / 'simulation/summary.json'
    result = json.loads(summary.read_text())
    assert result['native_validation_passed'] and result['deterministic']
    assert result['summary']['statuses']['invalid_endpoint'] == 0
    assert result['summary']['population'] == args.population
    assert len(result['samples']) == args.repeats
    assert not result['configuration']['navmesh'] and not result['configuration']['global_paths']
    assert result['configuration']['exact_buildings'] == args.exact_buildings
    assert result['configuration']['steering_mode'] == args.steering
    assert result['configuration']['guidance_mode'] == args.guidance_mode
    if args.guidance_mode != 'none':
        assert result['guidance']['sha256'] == manifest['guidance_sha256'] == sha(guidance_source)
    assert result['configuration']['weight'] == args.weight
    assert result['configuration']['support_m'] == args.support
    assert result['configuration']['length_scale_m'] == args.length_scale
    assert result['configuration']['record_moves'] == (not args.no_move_log)
    assert result['configuration']['copy_aerial'] == (not args.no_aerial)
    if args.no_move_log:
        assert result['moves_file'] is None
        (output / 'audit.json').write_text(json.dumps({
            'skipped': True, 'reason': 'Full movement logging disabled for parameter screening; '
            'native geometry validation still ran for every update.'}, indent=2) + '\n')
        print('Simulation complete; full movement audit skipped because recording was disabled.', flush=True)
    else:
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
    parser.add_argument('--requests', type=Path, help='Reuse these exact prepared request bytes instead of sampling again')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--population', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=1515146562)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--warmups', type=int, default=1)
    parser.add_argument('--cpu', type=int, default=2)
    parser.add_argument('--weight', type=nonnegative_float, default=16.)
    parser.add_argument('--support', type=positive_float, default=3.)
    parser.add_argument('--length-scale', type=positive_float, default=1.)
    parser.add_argument('--exact-buildings', action='store_true', help='Use oriented building rectangle fields instead of circumscribed circles')
    parser.add_argument('--steering', choices=('filtered', 'normalized', 'tangent'), default='filtered')
    parser.add_argument('--guidance', type=Path, help='Sparse analytical bump and pocket geometry')
    parser.add_argument('--guidance-mode', choices=('none', 'hierarchy', 'pockets', 'combined'), default='none')
    parser.add_argument('--no-move-log', action='store_true', help='Skip full movement stream and independent stream audit for screening')
    parser.add_argument('--no-aerial', action='store_true', help='Use the source aerial without copying it into the simulation directory')
    run(parser.parse_args())
