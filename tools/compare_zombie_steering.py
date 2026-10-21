#!/usr/bin/env python3
"""Run matched five-way tree-steering trials and audit full trajectories."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import struct
import subprocess

import numpy as np
import shapely

MODES = ['collision_only', 'gradient', 'gradient_side', 'perpendicular_push', 'detour']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def trajectory_audit(path, scene, radius):
    """GEOS AABB broad phase, independent vectorized double circle distances."""
    with path.open('rb') as stream:
        magic, population, frames = struct.unpack('<8sII', stream.read(16))
    assert magic == b'GDST001\0'
    assert population == len(scene['requests']) and frames > 0
    assert path.stat().st_size == 16 + 8*population*frames
    points = np.memmap(path, dtype='<f4', mode='r', offset=16, shape=(frames, population, 2))
    trunks = scene['geometry']['trunks']
    checked = pairs_count = penetrations = 0
    minimum = None
    if trunks:
        centers = np.array([[t['base'][0], t['base'][2]] for t in trunks], dtype=np.float64)
        radii = np.array([t['radius'] + radius for t in trunks], dtype=np.float64)
        tree = shapely.STRtree(shapely.box(centers[:, 0]-radii, centers[:, 1]-radii,
                                         centers[:, 0]+radii, centers[:, 1]+radii))
    for first in range(0, frames-1, max(1, 100000//population)):
        last = min(frames-1, first+max(1, 100000//population))
        a = np.asarray(points[first:last], dtype=np.float64).reshape((-1, 2))
        b = np.asarray(points[first+1:last+1], dtype=np.float64).reshape((-1, 2))
        assert np.isfinite(a).all() and np.isfinite(b).all(), ('Nonfinite trajectory', path)
        checked += len(a)
        if not trunks:
            continue
        pairs = tree.query(shapely.linestrings(np.stack((a, b), axis=1)))
        if not pairs.shape[1]:
            continue
        indices, obstacles = pairs
        start, delta = a[indices], b[indices]-a[indices]
        length2 = np.einsum('ij,ij->i', delta, delta)
        t = np.divide(np.einsum('ij,ij->i', centers[obstacles]-start, delta), length2,
                      out=np.zeros(len(indices)), where=length2 > 0).clip(0, 1)
        separation = start + t[:, None]*delta - centers[obstacles]
        clearance = np.sqrt(np.einsum('ij,ij->i', separation, separation)) - radii[obstacles]
        value = float(clearance.min())
        minimum = value if minimum is None else min(minimum, value)
        penetrations += int(np.count_nonzero(clearance < 0))
        pairs_count += len(indices)
    assert penetrations == 0, (path, minimum, penetrations)
    return {'frames': frames, 'population': population, 'checked_frame_segments': checked,
            'broadphase_pairs': pairs_count, 'penetrations': penetrations,
            'minimum_candidate_clearance_m': minimum, 'file_sha256': sha(path)}


def run(args):
    root, output = Path.cwd(), args.output.resolve()
    (output/'binaries').mkdir(parents=True, exist_ok=True)
    binary = output/'binaries/steering'
    if binary.exists():
        raise ValueError('Use a new output directory; existing results are preserved')
    shutil.copy2(args.binary, binary)
    files = ['CMakeLists.txt', 'src/collision_world.cpp', 'src/collision_world.hpp',
             'src/zombie_movement.cpp', 'src/zombie_movement.hpp', 'src/zombie_crowd.cpp',
             'src/zombie_crowd.hpp', 'src/character_step.cpp', 'src/character_step.hpp',
             'tests/test_collision.cpp', 'tests/test_tree_collision.cpp']
    for directory, pattern in [('src','zombie_steering*'), ('src','zombie_path_follow*'),
                               ('src','zombie_navmesh*'), ('tests','test_zombie_steering*'),
                               ('tests','test_zombie_path_follow*'), ('tools','*zombie_steering*')]:
        files += [str(p.relative_to(root)) for p in sorted((root/directory).glob(pattern)) if p.is_file()]
    files += [str(p.relative_to(root)) for p in sorted((root/'third_party/recastnavigation').rglob('*')) if p.is_file()]
    for name in files:
        target = output/'source-snapshot'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/name, target)
    manifest = {'branch': subprocess.check_output(['git','branch','--show-current'], text=True).strip(),
                'parent_commit': subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip(),
                'binary_sha256': sha(binary), 'source_sha256': {name:sha(root/name) for name in files},
                'cpu': args.cpu, 'population': args.population, 'seed': args.seed, 'duration_seconds': args.duration,
                'timestep_seconds': .05, 'maximum_speed_m_s': 2, 'repeats_per_process': args.repeats,
                'warmups_per_process': 1, 'order': MODES+list(reversed(MODES)),
                'compiler': subprocess.check_output(['c++','--version'], text=True).splitlines()[0],
                'flags': '-O3 -DNDEBUG'}
    (output/'source.diff').write_bytes(subprocess.check_output(['git','diff','HEAD','--binary']))
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    documents, previous_agents, previous_trajectories, scene_digests, processes = {}, {}, {}, {}, []
    rows = []
    for number, mode in enumerate(manifest['order'], 1):
        destination = output/f'{number:02d}-{mode}'
        assert not destination.exists()
        command = ['taskset','-c',str(args.cpu),str(binary),'--mode',mode,'--scenario','all',
                   '--population',str(args.population),'--seed',str(args.seed),'--duration',str(args.duration),
                   '--timestep','.05','--speed','2','--repeats',str(args.repeats),'--warmups','1',
                   '--output',str(destination)]
        profile = output/f'{number:02d}-process.json'
        print(f'Running {number}/10: {mode}', flush=True)
        with (output/f'{number:02d}.log').open('w') as log:
            subprocess.run(['/usr/bin/time','-o',str(profile),'-f',
                '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"peak_rss_kib":%M}',
                *command], stdout=log, stderr=subprocess.STDOUT, check=True)
        document = json.loads((destination/'summary.json').read_text())
        assert document['validation_passed']
        assert document['configuration']['heuristic_weight'] == 8
        assert not document['configuration']['agent_agent_collisions']
        assert not document['configuration']['trees_in_global_navmesh']
        for scene in document['scenarios']:
            name = scene['name']
            scene_digest = digest({'geometry':scene['geometry'], 'requests':scene['requests']})
            assert scene_digests.setdefault(name, scene_digest) == scene_digest
            result, = scene['modes']
            assert result['mode'] == mode
            key = (name, mode)
            if key in previous_agents:
                assert previous_agents[key] == result['agents'], ('Nondeterministic trajectories', key)
            previous_agents[key] = result['agents']
            trajectory_hash = sha(destination/result['trajectory']['file'])
            assert previous_trajectories.setdefault(key, trajectory_hash) == trajectory_hash, ('Nondeterministic full trajectory', key)
            documents.setdefault(key, []).append((destination, scene, result))
            for sample in result['samples']:
                assert sample['validation']['penetration_reporting_tolerance_m'] == 0
                assert sample['validation']['swept_penetrations'] == sample['validation']['endpoint_penetrations'] == 0
                rows.append({'process':number,'scenario':name,'mode':mode,**sample})
        with (destination/'steps.csv').open() as stream:
            steps = list(csv.DictReader(stream))
        first5 = [r for r in steps if r['scenario']=='woodland' and int(r['step'])<100 and int(r['active'])==args.population]
        assert len(first5) == 100*args.repeats, 'Fixed-load window lost active agents'
        processes.append({'process':number,'mode':mode,'output':str(destination),'command':command,
                          'profile':json.loads(profile.read_text()),'first5_steps':first5})
        (output/'processes.json').write_text(json.dumps(processes, indent=2)+'\n')
        woodland = next(s for s in document['scenarios'] if s['name']=='woodland')['modes'][0]['summary']
        print(f"  woodland: {woodland['arrived']}/{args.population}, "
              f"{woodland['first_5s_all_active_step_ms']['mean']:.3f} ms/step, "
              f"{woodland['controller_ns_per_active_update']:.1f} ns/controller-update", flush=True)
    comparison = {'configuration':manifest, 'scene_sha256':scene_digests, 'scenarios':{}, 'processes':processes}
    for (name, mode), runs in documents.items():
        samples = [sample for _,_,result in runs for sample in result['samples']]
        _, scene, result = runs[-1]
        audit = trajectory_audit(runs[-1][0]/result['trajectory']['file'], scene,
                                  float(np.float32(.3)))
        updates = sum(s['updates'] for s in samples)
        entry = {'arrived':result['summary']['arrived'],'population':result['summary']['population'],
                 'stalled':result['summary']['stalled'],'samples':samples,
                 'total_ms_median':statistics.median(s['total_ms'] for s in samples),
                 'controller_ns_per_active_update':sum(s['controller_ms'] for s in samples)*1e6/updates,
                 'total_ns_per_active_update':sum(s['total_ms'] for s in samples)*1e6/updates,
                 'summary':result['summary'],'independent_validation':audit,
                 'representative_output':str(runs[-1][0]),'trajectory_file':result['trajectory']['file']}
        if name=='woodland':
            window = [r for p in processes if p['mode']==mode for r in p['first5_steps']]
            values = [float(r['total_ms']) for r in window]
            entry['fixed_1000_active_step_ms'] = {'mean':statistics.mean(values),'median':statistics.median(values),
                'p95':float(np.percentile(values,95)),'min':min(values),'max':max(values),'count':len(values)}
            entry['fixed_load_component_ms'] = {field:statistics.mean(float(r[field]) for r in window)
                for field in ['query_ms','controller_ms','movement_ms','navmesh_ms','swept_trunk_guard_ms'] if field in window[0]}
        comparison['scenarios'].setdefault(name, {'purpose':scene['purpose'],'modes':{}})['modes'][mode]=entry
    for name, scenario in comparison['scenarios'].items():
        successful = {mode: {agent['id'] for agent in previous_agents[(name, mode)] if agent['arrived']}
                      for mode in MODES}
        common = set.intersection(*successful.values())
        reached = set.union(*successful.values())
        scenario['common_arrived_ids'] = sorted(common)
        scenario['common_arrived_count'] = len(common)
        scenario['reached_by_any_count'] = len(reached)
        scenario['reached_by_none_count'] = scenario['modes'][MODES[0]]['population']-len(reached)
        requests = documents[(name, MODES[0])][-1][1]['requests']
        planned = {request['id']: sum(float(np.linalg.norm(np.subtract(b,a)))
                   for a,b in zip(request['path'],request['path'][1:])) for request in requests}
        for mode in MODES:
            agents = [agent for agent in previous_agents[(name, mode)] if agent['id'] in common]
            def describe(values):
                return {'count': len(values), 'mean': statistics.mean(values) if values else None,
                        'median': statistics.median(values) if values else None,
                        'p95': float(np.percentile(values,95)) if values else None}
            scenario['modes'][mode]['common_success'] = {
                'count': len(agents),
                'travel_m': describe([agent['travel_m'] for agent in agents]),
                'travel_to_planned_ratio': describe([agent['travel_m']/planned[agent['id']] for agent in agents]),
                'arrival_seconds': describe([agent['arrival_seconds'] for agent in agents]),
                'heading_degrees_per_meter': describe([agent['heading_radians']*180/np.pi/agent['travel_m']
                                                       for agent in agents if agent['travel_m']>0])}
    for name, fingerprint in manifest['source_sha256'].items():
        assert sha(root/name)==fingerprint, ('Source changed during benchmark',name)
    assert sha(binary)==manifest['binary_sha256']
    (output/'comparison.json').write_text(json.dumps(comparison, indent=2)+'\n')
    fields = ['process','scenario','mode','repeat','population','arrived','not_arrived','stalled','updates',
              'total_ms','controller_ms','controller_ns_per_active_update','collision_interventions','navmesh_rejections']
    with (output/'measurements.csv').open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=fields,extrasaction='ignore',lineterminator='\n')
        writer.writeheader();writer.writerows(rows)
    print(output/'comparison.json', flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--population',type=int,default=1000)
    p.add_argument('--seed',type=int,default=1515146562)
    p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--duration',type=float,default=60)
    p.add_argument('--cpu',type=int,default=2)
    run(p.parse_args())
