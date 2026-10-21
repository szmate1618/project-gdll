#!/usr/bin/env python3
"""Audit every accepted movement against original physical replay obstacles."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import shapely

from prepare_potential_surface_requests import PhysicalGeometry

RECORD = np.dtype([('id', '<u4'), ('start', '<f8', (2,)), ('end', '<f8', (2,))])


def rectangle_test(start, end, axes, perpendiculars, halves):
    """Closed segment/OBB slabs and exact distance for nonintersecting pairs."""
    a = np.stack((np.einsum('ij,ij->i', start, axes),
                  np.einsum('ij,ij->i', start, perpendiculars)), axis=1)
    b = np.stack((np.einsum('ij,ij->i', end, axes),
                  np.einsum('ij,ij->i', end, perpendiculars)), axis=1)
    delta = b-a
    low, high = np.zeros(len(a)), np.ones(len(a))
    possible = np.ones(len(a), dtype=bool)
    for axis in range(2):
        parallel = delta[:, axis] == 0
        possible &= ~(parallel & (np.abs(a[:, axis]) > halves[:, axis]))
        first = np.divide(-halves[:, axis]-a[:, axis], delta[:, axis],
                          out=np.full(len(a), -np.inf), where=~parallel)
        last = np.divide(halves[:, axis]-a[:, axis], delta[:, axis],
                         out=np.full(len(a), np.inf), where=~parallel)
        low = np.maximum(low, np.minimum(first, last))
        high = np.minimum(high, np.maximum(first, last))
    hits = possible & (low <= high)
    outside_a, outside_b = np.maximum(np.abs(a)-halves, 0), np.maximum(np.abs(b)-halves, 0)
    squared = np.einsum('ij,ij->i', delta, delta)
    distances = np.minimum(np.linalg.norm(outside_a, axis=1), np.linalg.norm(outside_b, axis=1))
    for signs in ((-1,-1),(-1,1),(1,-1),(1,1)):
        corner = halves*np.asarray(signs)
        t = np.divide(np.einsum('ij,ij->i', corner-a, delta), squared,
                      out=np.zeros(len(a)), where=squared > 0).clip(0, 1)
        distances = np.minimum(distances, np.linalg.norm(a+t[:, None]*delta-corner, axis=1))
    distances[hits] = 0
    return hits, distances


def audit(scene_path, summary_path, output, chunk_size=100000):
    scene_raw = scene_path.read_bytes()
    scene = json.loads(scene_raw)
    document = json.loads(summary_path.read_text())
    source_sha = hashlib.sha256(scene_raw).hexdigest()
    requests = document['requests']
    geometry = PhysicalGeometry(scene)
    source = document.get('source_sha256', document.get('configuration', {}).get('source_sha256'))
    if source is not None and source != source_sha:
        raise ValueError('Summary/source fingerprint mismatch')
    moved = Path(document['moves_file'])
    if not moved.is_absolute():
        moved = summary_path.parent/moved
    if (moved.stat().st_size-8) % RECORD.itemsize:
        raise ValueError('Truncated movement stream')
    counts = {'segments': 0, 'candidate_pairs': 0, 'circle_pairs': 0, 'rectangle_pairs': 0,
              'colliding_pairs': 0, 'colliding_segments': 0, 'map_boundary_violations': 0,
              'invalid_record_ids': 0, 'invalid_endpoints': 0, 'nonfinite_records': 0,
              'discontinuous_segments': 0, 'final_position_mismatches': 0,
              'invalid_final_positions': 0, 'record_count_mismatch': 0}
    examples = []
    minimum_circle = minimum_rectangle = None
    request_ids = {int(r['id']) for r in requests}
    if len(request_ids) != len(requests):
        raise ValueError('Duplicate request IDs')
    agent_ids = [int(agent['id']) for agent in document['agents']]
    if len(set(agent_ids)) != len(agent_ids) or set(agent_ids) != request_ids:
        raise ValueError('Final agent IDs differ from request IDs')
    last_positions = {int(r['id']): np.asarray(r['start'], dtype=np.float64) for r in requests}
    for request in requests:
        for name in ('start', 'goal'):
            if not geometry.clear(request[name]):
                counts['invalid_endpoints'] += 1
    with moved.open('rb') as stream:
        if stream.read(8) != b'PSSMOV01':
            raise ValueError('Unknown movement stream format')
        while True:
            records = np.fromfile(stream, dtype=RECORD, count=chunk_size)
            if not len(records):
                break
            start, end = records['start'], records['end']
            offset = counts['segments']
            counts['segments'] += len(records)
            valid = np.isfinite(start).all(axis=1) & np.isfinite(end).all(axis=1)
            counts['nonfinite_records'] += int(np.count_nonzero(~valid))
            counts['invalid_record_ids'] += int(np.count_nonzero(~np.isin(records['id'], list(request_ids))))
            order = np.argsort(records['id'], kind='stable')
            split = np.r_[0, np.flatnonzero(np.diff(records['id'][order]))+1, len(order)]
            for first, last in zip(split, split[1:]):
                indices = order[first:last]
                agent_id = int(records['id'][indices[0]])
                if agent_id not in last_positions:
                    continue
                counts['discontinuous_segments'] += int(np.any(start[indices[0]] != last_positions[agent_id]))
                counts['discontinuous_segments'] += int(np.count_nonzero(np.any(start[indices[1:]] != end[indices[:-1]], axis=1)))
                last_positions[agent_id] = end[indices[-1]].copy()
            inside = ((start > geometry.minimum).all(axis=1) & (start < geometry.maximum).all(axis=1) &
                      (end > geometry.minimum).all(axis=1) & (end < geometry.maximum).all(axis=1))
            counts['map_boundary_violations'] += int(np.count_nonzero(~inside & valid))
            if not valid.all():
                continue
            pairs = geometry.tree.query(shapely.linestrings(np.stack((start, end), axis=1)))
            if not pairs.shape[1]:
                continue
            segments, obstacles = pairs
            counts['candidate_pairs'] += len(segments)
            circles = geometry.kinds[obstacles] == 1
            all_hits = np.zeros(len(segments), dtype=bool)
            if circles.any():
                si, oi = segments[circles], obstacles[circles]
                a, delta = start[si], end[si]-start[si]
                squared = np.einsum('ij,ij->i', delta, delta)
                t = np.divide(np.einsum('ij,ij->i', geometry.centers[oi]-a, delta), squared,
                              out=np.zeros(len(si)), where=squared > 0).clip(0, 1)
                clearance = np.linalg.norm(a+t[:, None]*delta-geometry.centers[oi], axis=1)-geometry.radii[oi]
                all_hits[circles] = clearance <= 0
                value = float(clearance.min())
                minimum_circle = value if minimum_circle is None else min(minimum_circle, value)
                counts['circle_pairs'] += len(si)
            if (~circles).any():
                si, oi = segments[~circles], obstacles[~circles]
                hits, clearance = rectangle_test(start[si]-geometry.centers[oi], end[si]-geometry.centers[oi],
                    geometry.axes[oi], geometry.perpendiculars[oi], geometry.halves[oi])
                all_hits[~circles] = hits
                value = float(clearance.min())
                minimum_rectangle = value if minimum_rectangle is None else min(minimum_rectangle, value)
                counts['rectangle_pairs'] += len(si)
            counts['colliding_pairs'] += int(all_hits.sum())
            counts['colliding_segments'] += len(np.unique(segments[all_hits]))
            for pair in np.flatnonzero(all_hits)[:max(0, 12-len(examples))]:
                si, oi = int(segments[pair]), int(obstacles[pair])
                examples.append({'record': offset+si, 'agent': int(records['id'][si]),
                                 'obstacle': oi, 'kind': int(geometry.kinds[oi]),
                                 'start': start[si].tolist(), 'end': end[si].tolist()})
    for agent in document['agents']:
        if int(agent['id']) not in last_positions or np.any(last_positions[int(agent['id'])] != agent['final']):
            counts['final_position_mismatches'] += 1
        if not geometry.clear(agent['final']):
            counts['invalid_final_positions'] += 1
    counts['record_count_mismatch'] = int(counts['segments'] != document['summary']['moved_segments'])
    passed = not any(counts[key] for key in ('colliding_pairs', 'map_boundary_violations',
                     'invalid_record_ids', 'invalid_endpoints', 'nonfinite_records',
                     'discontinuous_segments', 'final_position_mismatches',
                     'invalid_final_positions', 'record_count_mismatch'))
    result = {'passed': passed, 'source_sha256': source_sha, 'moves_sha256': file_sha256(moved),
              'moves_file': str(moved.resolve()), 'record_bytes': RECORD.itemsize,
              'geometry': 'Original normalized oriented rectangles, circles for trunks, exact physical padding .302 m (+.05 m buildings); closed contact is collision; no tolerance or radius shrink.',
              'minimum_circle_candidate_clearance_m': minimum_circle,
              'minimum_rectangle_candidate_clearance_m': minimum_rectangle,
              'examples': examples, **counts}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    return result


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--summary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    raise SystemExit(0 if audit(arguments.scene, arguments.summary, arguments.output)['passed'] else 1)
