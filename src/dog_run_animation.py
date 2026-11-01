"""Append the retargeted run without re-exporting licensed dog geometry or clips."""
from __future__ import annotations

import copy
import hashlib
import json

import numpy as np

from .glb import pack_glb, read_glb


def rig_fingerprint(document):
    data = {key: document[key] for key in ('nodes', 'skins')}
    return hashlib.sha256(json.dumps(data, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def append_run(source: bytes, motion: dict) -> bytes:
    document, binary = read_glb(source)
    if motion.get('schema_version') != 1 or motion.get('rig_sha256') != rig_fingerprint(document):
        raise ValueError('Run animation does not match the dog rig')
    if motion.get('reference_speed_mps') != 3:
        raise ValueError('Run reference speed must match the runtime clip contract: 3 m/s')
    if any(a.get('name', '').lower() in ('run', 'running') for a in document.get('animations', [])):
        raise ValueError('Dog already contains a run clip; rebuild from the cached original')
    times = np.asarray(motion['times'], dtype='<f4')
    if times.ndim != 1 or len(times) < 3 or not np.all(np.isfinite(times)) or times[0] != 0 or \
            not np.all(np.diff(times) > 0):
        raise ValueError('Run requires finite, increasing times starting at zero')
    joints = {document['nodes'][i]['name']: i for i in document['skins'][0]['joints']}
    result = copy.deepcopy(document)
    binary = bytearray(binary)

    def accessor(values, kind):
        binary.extend(b'\0' * (-len(binary) % 4))
        offset = len(binary)
        binary.extend(values.tobytes())
        view = len(result['bufferViews'])
        result['bufferViews'].append({'buffer': 0, 'byteOffset': offset, 'byteLength': values.nbytes})
        index = len(result['accessors'])
        result['accessors'].append({'bufferView': view, 'componentType': 5126,
                                    'count': len(values), 'type': kind,
                                    'min': values.reshape(len(values), -1).min(axis=0).tolist(),
                                    'max': values.reshape(len(values), -1).max(axis=0).tolist()})
        return index

    time_index = accessor(times, 'SCALAR')
    clip = {'name': 'run', 'samplers': [], 'channels': []}
    for name, tracks in motion['tracks'].items():
        if name not in joints:
            raise ValueError(f'Unknown dog joint: {name}')
        for path, values in tracks.items():
            if path not in ('rotation', 'translation') or (path == 'translation' and name != 'hips'):
                raise ValueError('Run may rotate joints and translate hips only')
            values = np.asarray(values, dtype='<f4')
            width = 4 if path == 'rotation' else 3
            if values.shape != (len(times), width) or not np.all(np.isfinite(values)) or \
                    not np.allclose(values[0], values[-1], atol=1e-6):
                raise ValueError(f'Invalid or non-looping run track: {name}/{path}')
            if path == 'rotation' and not np.allclose(np.linalg.norm(values, axis=1), 1, atol=1e-4):
                raise ValueError(f'Non-unit run quaternion: {name}')
            if path == 'translation' and not np.allclose(values[:, [0, 2]], values[0, [0, 2]], atol=1e-6):
                raise ValueError('Run must stay in place horizontally')
            clip['channels'].append({'sampler': len(clip['samplers']),
                                     'target': {'node': joints[name], 'path': path}})
            clip['samplers'].append({'input': time_index, 'output': accessor(values, 'VEC4' if width == 4 else 'VEC3'),
                                     'interpolation': 'LINEAR'})
    if not clip['channels']:
        raise ValueError('Run has no animated joints')
    result.setdefault('animations', []).append(clip)
    result['buffers'][0]['byteLength'] = len(binary)
    output = pack_glb(result, bytes(binary))
    read_glb(output)
    return output
