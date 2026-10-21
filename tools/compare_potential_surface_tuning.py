#!/usr/bin/env python3
"""Compare locked PSS parameters with baseline on training and untouched trips."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import sys

import numpy as np
import shapely


def distribution(values):
    return {'count':len(values),'mean':statistics.mean(values) if values else None,
            'median':statistics.median(values) if values else None,
            'p95':float(np.percentile(values,95)) if values else None}


def contacts(document,geometry):
    counts={'building':0,'tree':0,'fence':0,'map_boundary':0,'off_contact':0}
    clipped=0
    for agent in document['agents']:
        if agent['status']!='stalled':continue
        clipped+=agent['clipped_moves']>0
        point=np.asarray(agent['final'])
        candidates=geometry.tree.query(shapely.box(point[0]-1,point[1]-1,point[0]+1,point[1]+1))
        edge=float(min(np.min(point-geometry.minimum),np.min(geometry.maximum-point)))
        best=(edge,'map_boundary')
        for idx in candidates:
            offset=point-geometry.centers[idx]
            if geometry.kinds[idx]==1:
                distance=float(np.linalg.norm(offset)-geometry.radii[idx])
            else:
                local=np.array([np.dot(offset,geometry.axes[idx]),np.dot(offset,geometry.perpendiculars[idx])])
                distance=float(np.linalg.norm(np.maximum(np.abs(local)-geometry.halves[idx],0)))
            if distance<best[0]:best=(distance,['building','tree','fence'][int(geometry.kinds[idx])])
        counts[best[1] if best[0]<=1e-5 else 'off_contact']+=1
    return {'counts':counts,'stalled_with_clipped_moves':clipped,
            'near_contact_threshold_m':1e-5,'threshold_is_not_a_collision_tolerance':True}


def run(args):
    sys.path.insert(0,str(args.root/'tools'))
    from prepare_potential_surface_requests import PhysicalGeometry
    winner=json.loads(args.winner.read_text())
    comparison={'locked_configuration':winner['configuration'],'holdout_seed':winner['holdout_seed'],
                'scenarios':{}}
    geometry=None
    for name in ('training','holdout'):
        documents={}
        entries={}
        fixed_frames={}
        for variant in ('baseline','tuned'):
            output=args.directory/f'{name}-{variant}'
            document=json.loads((output/'simulation/summary.json').read_text())
            audit=json.loads((output/'audit.json').read_text())
            assert document['native_validation_passed'] and document['deterministic'] and audit['passed']
            assert document['summary']['population']==1000 and len(document['samples'])==1
            assert document['summary']['statuses']['invalid_endpoint']==0
            assert all(s['validation_violations']==s['invalid_moves']==0 for s in document['samples'])
            assert all(s['statuses']==document['summary']['statuses'] for s in document['samples'])
            c=document['configuration']
            assert c['seed']==(1515146562 if name=='training' else winner['holdout_seed'])
            assert audit['source_sha256']==document['source_sha256']
            assert audit['segments']==document['summary']['moved_segments']
            expected={'weight':16.,'support':3.,'length_scale':1.} if variant=='baseline' else winner['configuration']
            assert (c['weight'],c['support_m'],c['length_scale_m'])==(expected['weight'],expected['support'],expected['length_scale'])
            assert c['timestep_seconds']==.05 and c['speed_m_per_second']==2
            assert c['radius_m']==.3 and c['skin_m']==.002 and c['building_extra_m']==.05
            if geometry is None:geometry=PhysicalGeometry(json.loads(Path(document['source']).read_text()))
            with (output/'simulation/steps.csv').open() as stream:
                fixed=[row for row in csv.DictReader(stream) if int(row['step'])<100 and int(row['active'])==1000]
            assert fixed
            fixed_frames[variant]={(int(row['repeat']),int(row['step'])):row for row in fixed}
            assert len(fixed_frames[variant])==len(fixed)
            entry={'configuration':c,'statuses':document['summary']['statuses'],
                   'fixed_all_active_cpu_ms':distribution([float(row['cpu_ms']) for row in fixed]),
                   'fixed_all_active_elapsed_ms':distribution([float(row['total_ms']) for row in fixed]),
                   'total_cpu_ms_median':statistics.median(s['cpu_ms'] for s in document['samples']),
                   'total_elapsed_ms_median':statistics.median(s['total_ms'] for s in document['samples']),
                   'cpu_ns_per_update':sum(s['cpu_ms'] for s in document['samples'])*1e6/sum(s['updates'] for s in document['samples']),
                   'updates':document['summary']['updates'],'query_stats':document['summary']['query_stats'],
                   'world_bytes':document['summary']['world_bytes'],
                   'contact_diagnostic':contacts(document,geometry),'audit':audit,
                   'summary_file':str(output/'simulation/summary.json')}
            entries[variant]=entry;documents[variant]=document
        baseline,tuned=documents['baseline'],documents['tuned']
        assert baseline['requests_sha256']==tuned['requests_sha256']
        assert baseline['requests']==tuned['requests'] and baseline['source_sha256']==tuned['source_sha256']
        agents={variant:{a['id']:a for a in doc['agents']} for variant,doc in documents.items()}
        reached={variant:{a['id'] for a in doc['agents'] if a['status']=='arrived'} for variant,doc in documents.items()}
        common=reached['baseline']&reached['tuned']
        matched_frames=sorted(set(fixed_frames['baseline'])&set(fixed_frames['tuned']))
        assert matched_frames
        for variant,entry in entries.items():
            entry['fixed_all_active_available_frames']=len(fixed_frames[variant])
            entry['fixed_all_active_cpu_ms']=distribution([float(fixed_frames[variant][key]['cpu_ms']) for key in matched_frames])
            entry['fixed_all_active_elapsed_ms']=distribution([float(fixed_frames[variant][key]['total_ms']) for key in matched_frames])
            shared=[agents[variant][idx] for idx in sorted(common)]
            entry['common_success']={'count':len(shared),
                'travel_m':distribution([a['travel_m'] for a in shared]),
                'travel_to_direct_ratio':distribution([a['travel_m']/a['initial_distance_m'] for a in shared]),
                'arrival_seconds':distribution([a['arrival_seconds'] for a in shared])}
        scenario={'variants':entries,'requests_sha256':baseline['requests_sha256'],
            'matched_all_active_timing_frames':len(matched_frames),'full_first_5s_matched':len(matched_frames)==100,
            'paired':{'both_arrive':len(common),'baseline_only':len(reached['baseline']-reached['tuned']),
                'tuned_only':len(reached['tuned']-reached['baseline']),
                'neither':1000-len(reached['baseline']|reached['tuned']),
                'tuned_only_ids':sorted(reached['tuned']-reached['baseline']),
                'lost_ids':sorted(reached['baseline']-reached['tuned'])},
            'common_success_travel_change_percent':distribution([100*(agents['tuned'][idx]['travel_m']/agents['baseline'][idx]['travel_m']-1) for idx in common]),
            'common_success_arrival_change_percent':distribution([100*(agents['tuned'][idx]['arrival_seconds']/agents['baseline'][idx]['arrival_seconds']-1) for idx in common])}
        comparison['scenarios'][name]=scenario
    assert comparison['scenarios']['training']['requests_sha256']!=comparison['scenarios']['holdout']['requests_sha256']
    args.output.write_text(json.dumps(comparison,indent=2)+'\n')
    print(args.output)
    for name,scenario in comparison['scenarios'].items():
        print(name,scenario['paired'])
        for variant,entry in scenario['variants'].items():
            print(variant,entry['statuses'],entry['fixed_all_active_cpu_ms'],entry['contact_diagnostic'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--winner',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    run(parser.parse_args())
