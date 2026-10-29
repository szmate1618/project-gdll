#!/usr/bin/env python3
"""Compare both prism colorings as LOD3 replacements at the 80 m boundary.

Report quality relative to the original, matched-pose LOD2, and the sampled
10-to-5 Hz LOD2 transition separately. This is not a perceptual pass/fail test.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
from collections import defaultdict
from itertools import product
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from analyze_zombie_lod_quality import compare, font, read_sample, write_csv
from analyze_zombie_lod4_distance import KEYS, quantile, roi_for


VARIANTS = ('original', 'lod2', 'lod2_10hz', 'lod3', 'prism_original', 'prism_white')
SLIM_VARIANTS = ('prism_slim_original', 'prism_slim_white')
SLIM2_VARIANTS = ('prism_slim2_original', 'prism_slim2_white')
PRISM_PREFIXES = ('prism', 'prism_slim', 'prism_slim2')
GEOMETRY_LABELS = {'prism': 'Standard', 'prism_slim': 'Slim 1', 'prism_slim2': 'PrismSlim2'}
CANDIDATES = ('lod3', 'prism_original', 'prism_white')
PAIRS = tuple((reference, candidate) for reference in ('original', 'lod2', 'lod2_10hz')
              for candidate in CANDIDATES) + (
                  ('original', 'lod2'), ('original', 'lod2_10hz'),
                  ('lod2_10hz', 'lod2'), ('prism_original', 'prism_white'))
LABELS = {'original': 'Original', 'lod2': 'LOD2 · matched 5 Hz',
          'lod2_10hz': 'LOD2 · predecessor 10 Hz', 'lod3': 'Current LOD3',
          'prism_original': 'Prism · earlier sampled colors', 'prism_white': 'Prism · off-white vest',
          'prism_slim_original': 'Slim 1 prism · sampled colors', 'prism_slim_white': 'Slim 1 prism · off-white vest',
          'prism_slim2_original': 'PrismSlim2 · sampled colors', 'prism_slim2_white': 'PrismSlim2 · off-white vest'}
MEASURES = ('silhouette_iou', 'boundary_mean_px', 'boundary_p95_px', 'boundary_max_px',
            'silhouette_area_ratio', 'rgb_rmse_union', 'rgb_rmse_common_support',
            'rgb_rmse_original_support', 'native_changed_pixels_common',
            'native_missing_pixels', 'native_color_coverage_ratio',
            'native_color_missing_fraction', 'reference_native_pixels', 'candidate_native_pixels')


def variants_for(names):
    names = set(names)
    expected = VARIANTS
    for index, pair in enumerate((SLIM_VARIANTS, SLIM2_VARIANTS)):
        if names & set(pair):
            if not set(pair).issubset(names):
                raise ValueError(f'Both {GEOMETRY_LABELS[PRISM_PREFIXES[index + 1]]} prism color variants are required')
            if index and not set(SLIM_VARIANTS).issubset(names):
                raise ValueError('Slim 2 captures require the Slim 1 baseline pair')
            expected += pair
    if names != set(expected):
        raise ValueError('Missing or unexpected capture variants')
    return expected


def prism_prefixes(variants):
    return tuple(prefix for prefix in PRISM_PREFIXES if f'{prefix}_original' in variants)


def display_variants(variants, reference):
    prefixes = prism_prefixes(variants)
    if len(prefixes) == 1:
        return (reference, 'lod2', 'lod3', 'prism_original', 'prism_white')
    return (reference, 'lod3') + tuple(f'{prefix}_{mode}' for prefix in prefixes[-2:] for mode in ('original', 'white'))


def pairs_for(variants):
    pairs = PAIRS
    prefixes = prism_prefixes(variants)
    for previous, prefix in zip(prefixes, prefixes[1:]):
        pair = (f'{prefix}_original', f'{prefix}_white')
        pairs += tuple((reference, candidate) for reference in ('original', 'lod2', 'lod2_10hz') for candidate in pair)
        pairs += ((pair[0], pair[1]),) + tuple((f'{previous}_{mode}', f'{prefix}_{mode}') for mode in ('original', 'white'))
    return pairs


def load_groups(path, metadata):
    if metadata['distances_m'] != [80]:
        raise ValueError('This LOD3 replacement analysis requires exactly the 80 m capture')
    groups = defaultdict(dict)
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            for field in ('phase', 'angle', 'distance', 'elevation', 'pixel_offset_x',
                          'pixel_offset_y', 'animation_frame', 'animation_hz'):
                row[field] = float(row[field])
            for field in ('triangles', 'vertices', 'width', 'height', 'crop_x', 'crop_y', 'mask_scale', 'pixel_phase'):
                row[field] = int(row[field])
            key = tuple(row[field] for field in KEYS)
            if row['variant'] in groups[key]:
                raise ValueError(f'Duplicate render: {key}, {row["variant"]}')
            groups[key][row['variant']] = row
    if not groups:
        raise ValueError('No captures')
    variants = variants_for(set().union(*(set(group) for group in groups.values())))
    dimensions = {'width': metadata['crop_size'], 'height': metadata['crop_size'],
                  'crop_x': metadata['crop_x'], 'crop_y': metadata['crop_y'],
                  'mask_scale': metadata['mask_scale']}
    for key, group in groups.items():
        if set(group) != set(variants):
            raise ValueError(f'Missing or unexpected variants: {key}')
        if any(row[field] != value for row in group.values() for field, value in dimensions.items()):
            raise ValueError(f'Unmatched capture geometry: {key}')
        matched = [row for variant, row in group.items() if variant != 'lod2_10hz']
        if len({row['animation_frame'] for row in matched}) != 1:
            raise ValueError(f'Unmatched 5 Hz animation frame: {key}')
        if any(row['animation_hz'] != 5 for row in matched) or group['lod2_10hz']['animation_hz'] != 10:
            raise ValueError(f'Expected 5 Hz poses and a 10 Hz LOD2 predecessor: {key}')
    number = lambda value: float(f'{value:.6g}')  # C++ CSV stream precision.
    expected = set()
    for asset, clip, pose, yaw, distance, elevation, pixel in product(
            {key[0] for key in groups}, ('idle', 'walk', 'run'),
            range(metadata['poses_per_clip']), range(metadata['angles']), metadata['distances_m'],
            metadata['elevations_degrees'], enumerate(metadata['pixel_offsets'])):
        pixel_phase, (offset_x, offset_y) = pixel
        expected.add((asset, clip, number(pose / metadata['poses_per_clip']),
                      number(yaw * 360 / metadata['angles']), number(distance),
                      number(elevation), number(offset_x), number(offset_y), pixel_phase))
    if set(groups) != expected:
        raise ValueError(f'Incomplete capture product: {len(expected - set(groups))} missing, '
                         f'{len(set(groups) - expected)} unexpected paired views')
    if metadata['samples'] != len(groups) * len(variants):
        raise ValueError('Metadata sample count differs from complete paired views')
    return dict(groups)


def measure_group(root, group):
    variants = variants_for(group)
    samples = {variant: read_sample(root, group[variant]) for variant in variants}
    for prefix in prism_prefixes(variants):
        old, white = samples[f'{prefix}_original'], samples[f'{prefix}_white']
        if old['bounds'] != white['bounds'] or not np.array_equal(old['mask'], white['mask']):
            raise ValueError(f'{prefix} color variants have different depth masks: {group["original"]}')
    common = np.logical_or.reduce([sample['native_mask'] for sample in samples.values()])
    original_support = samples['original']['native_mask']
    rows = []
    for reference, candidate in pairs_for(variants):
        source, target = samples[reference], samples[candidate]
        row = {field: group[candidate][field] for field in KEYS}
        row.update(reference=reference, candidate=candidate, comparison=f'{candidate}_vs_{reference}',
                   triangles=group[candidate]['triangles'], vertices=group[candidate]['vertices'],
                   reference_animation_frame=group[reference]['animation_frame'],
                   candidate_animation_frame=group[candidate]['animation_frame'],
                   reference_animation_hz=group[reference]['animation_hz'],
                   candidate_animation_hz=group[candidate]['animation_hz'])
        row.update(compare(source, target, group[candidate]['mask_scale']))
        difference = source['color'].astype(np.float32) - target['color'].astype(np.float32)
        row.update(rgb_rmse_common_support=float(np.sqrt(np.mean((difference[common] / 255) ** 2))),
                   rgb_rmse_original_support=float(np.sqrt(np.mean((difference[original_support] / 255) ** 2))),
                   native_changed_pixels_common=int((np.any(np.abs(difference) > 1, axis=2) & common).sum()),
                   native_missing_pixels=int((source['color_mask'] & ~target['color_mask']).sum()),
                   reference_native_pixels=int(source['color_mask'].sum()),
                   candidate_native_pixels=int(target['color_mask'].sum()))
        rows.append(row)
    return rows


def summarize(rows, elevation):
    result = {'comparison': rows[0]['comparison'], 'reference': rows[0]['reference'],
              'candidate': rows[0]['candidate'], 'elevation': elevation, 'views': len(rows),
              'triangles': rows[0]['triangles'], 'vertices': rows[0]['vertices']}
    for field in MEASURES:
        for percentile, suffix in ((50, 'median'), (95, 'p95'), (99, 'p99')):
            result[f'{field}_{suffix}'] = quantile(rows, field, percentile)
    result['reference_invisible_views'] = sum(row['reference_native_pixels'] == 0 for row in rows)
    result['candidate_invisible_views'] = sum(row['candidate_native_pixels'] == 0 for row in rows)
    result['newly_invisible_views'] = sum(row['reference_native_pixels'] > 0 and row['candidate_native_pixels'] == 0
                                          for row in rows)
    result['different_animation_frame_views'] = sum(row['reference_animation_frame'] != row['candidate_animation_frame']
                                                    for row in rows)
    return result


def summaries(metrics):
    pooled, elevated = defaultdict(list), defaultdict(list)
    for row in metrics:
        pooled[row['comparison']].append(row)
        elevated[(row['comparison'], row['elevation'])].append(row)
    return ([summarize(rows, 'all') for _, rows in sorted(pooled.items())] +
            [summarize(rows, key[1]) for key, rows in sorted(elevated.items())])


def angle_summaries(metrics):
    return dimension_summaries(metrics, 'angle')


def dimension_summaries(metrics, field):
    grouped = defaultdict(list)
    for row in metrics:
        grouped[(row['comparison'], row[field])].append(row)
    return [dict(summarize(rows, 'all'), **{field: key[1]}) for key, rows in sorted(grouped.items())]


def geometry_votes(metrics, baseline, candidate, breakdowns=('angle',)):
    """Paired changes in quality; retain equal views and the sign of each delta."""
    paired = defaultdict(dict)
    for row in metrics:
        if row['reference'] not in ('original', 'lod2', 'lod2_10hz'):
            continue
        for mode in ('original', 'white'):
            if row['candidate'] in (f'{baseline}_{mode}', f'{candidate}_{mode}'):
                paired[(row['reference'], mode, tuple(row[field] for field in KEYS))][row['candidate']] = row
    populations = defaultdict(list)
    for (reference, mode, key), pair in paired.items():
        populations[(reference, mode, 'all', 'all')].append(pair)
        for field in breakdowns:
            populations[(reference, mode, field, key[KEYS.index(field)])].append(pair)
    output = []
    for (reference, mode, breakdown, value), pairs in populations.items():
        result = {'reference': reference, 'color_mode': mode, 'baseline_geometry': baseline,
                  'candidate_geometry': candidate, 'breakdown': breakdown, 'value': value,
                  'views': len(pairs), 'metrics': {}}
        for field in ('silhouette_iou', 'boundary_p95_px', 'boundary_max_px', 'silhouette_area_ratio',
                      'rgb_rmse_common_support', 'rgb_rmse_original_support'):
            deltas = []
            for pair in pairs:
                standard, slim = (pair[f'{baseline}_{mode}'][field], pair[f'{candidate}_{mode}'][field])
                if field == 'silhouette_area_ratio':
                    standard, slim = abs(standard - 1), abs(slim - 1)
                deltas.append(slim - standard)
            signed = [-value if field == 'silhouette_iou' else value for value in deltas]
            name = 'silhouette_area_absolute_error' if field == 'silhouette_area_ratio' else field
            result['metrics'][name] = {
                'candidate_better_views': sum(value < -1e-9 for value in signed),
                'baseline_better_views': sum(value > 1e-9 for value in signed),
                'equal_views': sum(abs(value) <= 1e-9 for value in signed),
                'candidate_minus_baseline_mean': float(np.mean(deltas)),
                'candidate_minus_baseline_median': float(np.median(deltas))}
        output.append(result)
    return output


def slimming_votes(metrics):
    # Retain the eight-variant report's published JSON fields.
    output = []
    for row in geometry_votes(metrics, 'prism', 'prism_slim'):
        result = {field: row[field] for field in ('reference', 'color_mode', 'views')}
        result['angle'] = row['value']
        result['metrics'] = {field: {key.replace('candidate', 'slim').replace('baseline', 'standard'): value
                                     for key, value in measures.items()}
                             for field, measures in row['metrics'].items()}
        output.append(result)
    return output


def color_votes(metrics, prefix='prism'):
    paired = defaultdict(dict)
    for row in metrics:
        if row['reference'] == 'original' and row['candidate'] in (f'{prefix}_original', f'{prefix}_white'):
            paired[tuple(row[field] for field in KEYS)][row['candidate']] = row
    output = {}
    for measure in ('rgb_rmse_common_support', 'rgb_rmse_original_support'):
        differences = [pair[f'{prefix}_white'][measure] - pair[f'{prefix}_original'][measure] for pair in paired.values()]
        output[measure] = {'white_better_views': sum(value < -1e-9 for value in differences),
                           'old_better_views': sum(value > 1e-9 for value in differences),
                           'equal_views': sum(abs(value) <= 1e-9 for value in differences),
                           'white_minus_old_mean': float(np.mean(differences)),
                           'white_minus_old_median': float(np.median(differences))}
    return output


def visual_sheet(root, groups, output):
    # A fixed grid covers every yaw and all clips, with distinct sampled poses.
    selected = []
    for clip, phase in (('idle', 0.), ('walk', .25), ('run', .5)):
        for angle in range(0, 360, 45):
            choices = [key for key in groups if key[1] == clip and key[3] == angle]
            if choices:
                selected.append(min(choices, key=lambda key: (abs(key[2] - phase), abs(key[5]), key[8])))
    available = variants_for(next(iter(groups.values())))
    prefixes = prism_prefixes(available)
    variants = display_variants(available, 'original')
    width, row_height = max(1500, 20 + len(variants) * 295), 350
    sheet = Image.new('RGB', (width, 90 + row_height * len(selected)), '#14202e')
    draw = ImageDraw.Draw(sheet)
    title = ('80 m: original, current LOD3, ' + ' and '.join(GEOMETRY_LABELS[prefix] for prefix in prefixes[-2:])
             if len(prefixes) > 1 else '80 m: original, LOD2, current LOD3 and both prism colorings')
    draw.text((20, 12), title, font=font(23), fill='white')
    draw.text((20, 48), '1280×720, 60° vertical FOV. Native pixels above; nearest-neighbor 10× below. All variants here use matched 5 Hz poses.',
              font=font(14), fill='#cbd5e1')
    for index, key in enumerate(selected):
        group = groups[key]
        samples = {variant: read_sample(root, group[variant]) for variant in variants}
        roi = roi_for(samples)
        y = 90 + index * row_height
        draw.text((20, y), f'{key[1]} · phase {key[2]:g} · yaw {key[3]:g}° · elevation {key[5]:g}° · pixel phase {key[8]}',
                  font=font(15), fill='#9be0c4')
        for column, variant in enumerate(variants):
            x = 20 + column * 295
            draw.text((x, y + 25), f'{LABELS[variant]} · {group[variant]["triangles"]} tris', font=font(13), fill='white')
            patch = Image.fromarray(samples[variant]['color']).crop(roi)
            sheet.paste(patch, (x, y + 49))
            sheet.paste(patch.resize((patch.width * 10, patch.height * 10), Image.Resampling.NEAREST), (x, y + 85))
    sheet.save(output / 'angles-poses.png')
    preview_rows = [min((index for index, key in enumerate(selected) if key[1] == clip),
                        key=lambda index: abs(selected[index][3] - angle))
                    for clip, angle in (('idle', 0), ('walk', 90), ('run', 225))]
    preview = Image.new('RGB', (width, 90 + row_height * len(preview_rows)), '#14202e')
    preview.paste(sheet.crop((0, 0, width, 90)), (0, 0))
    for index, source in enumerate(preview_rows):
        y = 90 + source * row_height
        preview.paste(sheet.crop((0, y, width, y + row_height)), (0, 90 + index * row_height))
    preview.save(output / 'preview.png')
    front_index = min(range(len(selected)), key=lambda index: (selected[index][1] != 'idle', abs(selected[index][3])))
    front = Image.new('RGB', (width, 90 + row_height), '#14202e')
    front.paste(sheet.crop((0, 0, width, 90)), (0, 0))
    y = 90 + front_index * row_height
    front.paste(sheet.crop((0, y, width, y + row_height)), (0, 90))
    front.save(output / 'front-comparison.png')


def transition_sheet(root, groups, metrics, output):
    # Show an actual sampled pose-rate change, with every geometry alternative
    # drawn at that same condition. The aggregate metrics also retain cases
    # where both rates happen to select the same pose.
    available = variants_for(next(iter(groups.values())))
    prefixes = prism_prefixes(available)
    inspected = f'{prefixes[-1]}_white'
    choices = [row for row in metrics if row['comparison'] == f'{inspected}_vs_lod2_10hz']
    changed_poses = [row for row in choices if row['reference_animation_frame'] != row['candidate_animation_frame']]
    chosen = max(changed_poses or choices, key=lambda row: row['rgb_rmse_common_support'])
    key = tuple(chosen[field] for field in KEYS)
    group = groups[key]
    samples = {variant: read_sample(root, group[variant]) for variant in variants_for(group)}
    roi = roi_for(samples)
    variants = display_variants(available, 'lod2_10hz')
    sheet = Image.new('RGB', (max(1500, 20 + len(variants) * 295), 510), '#14202e')
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 14), '80 m LOD2 → LOD3 boundary: one sampled transition', font=font(23), fill='white')
    draw.text((20, 51), f'{key[1]}, phase {key[2]:g}, yaw {key[3]:g}°, elevation {key[5]:g}°, pixel phase {key[8]}',
              font=font(15), fill='#9be0c4')
    subject = f'{GEOMETRY_LABELS[prefixes[-1]]} white-prism'
    draw.text((20, 78), f'Largest {subject} RGB error among sampled 10→5 Hz pose changes. Native above; 12× below. Geometry and pose-rate effects both apply.',
              font=font(13), fill='#cbd5e1')
    for column, variant in enumerate(variants):
        x = 20 + column * 295
        draw.text((x, 112), LABELS[variant], font=font(13), fill='white')
        draw.text((x, 137), f'frame {group[variant]["animation_frame"]:g} · {group[variant]["animation_hz"]:g} Hz',
                  font=font(13), fill='#cbd5e1')
        patch = Image.fromarray(samples[variant]['color']).crop(roi)
        sheet.paste(patch, (x, 166))
        sheet.paste(patch.resize((patch.width * 12, patch.height * 12), Image.Resampling.NEAREST), (x, 202))
    sheet.save(output / 'boundary-comparison.png')
    return {field: chosen[field] for field in KEYS}


def write_report(output, payload):
    variants = payload.get('variants', VARIANTS)
    prefixes = prism_prefixes(variants)
    expanded = len(prefixes) > 1
    candidates = ('lod3',) + tuple(f'{prefix}_{mode}' for prefix in prefixes for mode in ('original', 'white'))
    document = '''<!doctype html><html><meta charset="utf-8"><title>Prism as LOD3 at 80 m</title>
<style>body{font:16px system-ui;background:#14202e;color:#edf3f8;margin:30px;max-width:1500px}a{color:#9bd9ff}
table{border-collapse:collapse;font-size:14px}th,td{padding:9px;border:1px solid #586271;text-align:right}img{max-width:100%;height:auto}pre{white-space:pre-wrap}</style>
<h1>80 m: prism as a replacement for LOD3</h1>
<p>One ZombieMale_C model, both the earlier sampled proxy colors and source-derived off-white vest. The prism color variants
have identical depth masks in every matched view. Original, matched LOD2, current LOD3 and both prisms use the same
5 Hz pose. A separate 10 Hz LOD2 reference samples the actual predecessor animation rate at the boundary.</p>
<p>Each table uses a distinct reference. Original-reference rows describe mesh fidelity at an identical pose;
LOD2-reference rows isolate geometric switching differences; 10 Hz LOD2-reference rows include the sampled animation-rate
change as well. This eight-pose survey is not continuous animation or a perceptual acceptance study. No unconditional
pass/fail criterion is applied.</p>
<p>RGB RMSE uses sRGB byte values normalized to 0–1. Common support is the union of all six variants' supersampled native
pixel masks in each view, so its support is identical across candidate colorings. Original support uses only the original
supersampled native pixel mask. Neither is a perceptual color metric. Coverage and shape differences remain separate.</p>'''
    if expanded:
        document = document.replace('both prisms', 'all prism candidates').replace('all six variants', f'all {len(variants)} variants')
        document += f'''<p>The slim variants change geometry while keeping the corresponding sampled or off-white color policy.
The common color support now includes all {len(variants)} variants. Compare candidates within this report; common-support RGB values
cannot be directly compared with reports using fewer variants. Original-mask support retains its original definition.</p>'''
    if prefixes[-1] == 'prism_slim2':
        document += '''<p>PrismSlim2 starts from Slim 1 and edits only its two upper rear torso corners: move them 15% closer
about their midpoint and lower both by 10% of torso height. The front upper apex and both color policies remain unchanged.</p>'''
    headers = ('Candidate', 'IoU median', 'Edge p95 median', 'Edge p95 across-view p95',
               'Max edge across-view p99', 'RGB RMSE common median', 'RGB RMSE original-mask median',
               'Silhouette area / reference median', 'Native color coverage median', 'Missing color fraction p95', 'Newly invisible')
    for reference in ('original', 'lod2', 'lod2_10hz'):
        document += f'<h2>Reference: {html.escape(LABELS[reference])}</h2><table><tr>'
        document += ''.join(f'<th>{header}</th>' for header in headers) + '</tr>'
        for row in payload['summary']:
            if row['reference'] != reference or row['candidate'] not in candidates or row['elevation'] != 'all':
                continue
            values = [LABELS[row['candidate']]] + [f'{row[field]:.3f}' for field in (
                'silhouette_iou_median', 'boundary_p95_px_median', 'boundary_p95_px_p95',
                'boundary_max_px_p99', 'rgb_rmse_common_support_median', 'rgb_rmse_original_support_median',
                'silhouette_area_ratio_median', 'native_color_coverage_ratio_median', 'native_color_missing_fraction_p95')] + [str(row['newly_invisible_views'])]
            document += '<tr>' + ''.join(f'<td>{value}</td>' for value in values) + '</tr>'
        document += '</table>'
    if expanded:
        document += '''<h2>Original-reference quality by yaw</h2><p>All clips, phases, elevations and pixel alignments.
Geometry metrics are identical for the two colorings within each geometry.</p><table><tr>
<th>Yaw</th><th>Geometry</th><th>IoU median</th><th>Area / original median</th><th>Edge p95 median</th>
<th>Edge p95 across-view p95</th><th>Sampled-color RGB common median</th><th>White-color RGB common median</th></tr>'''
        angle_lookup = {(row['angle'], row['candidate']): row for row in payload['angle_summary'] if row['reference'] == 'original'}
        for angle in sorted({key[0] for key in angle_lookup}):
            for prefix in prefixes[-2:]:
                label = GEOMETRY_LABELS[prefix]
                row, white = angle_lookup[(angle, f'{prefix}_original')], angle_lookup[(angle, f'{prefix}_white')]
                values = [f'{angle:g}°', label] + [f'{row[field]:.3f}' for field in (
                    'silhouette_iou_median', 'silhouette_area_ratio_median', 'boundary_p95_px_median',
                    'boundary_p95_px_p95', 'rgb_rmse_common_support_median')] + [f'{white["rgb_rmse_common_support_median"]:.3f}']
                document += '<tr>' + ''.join(f'<td>{value}</td>' for value in values) + '</tr>'
        document += '</table><p><a href="angle-summary.csv">Every comparison by yaw</a></p>'
    if 'clip_summary' in payload:
        document += '<p><a href="clip-summary.csv">Every comparison by animation clip</a></p>'
    document += '''<p><a href="summary.csv">Pooled and elevation summaries</a> · <a href="summary.json">Full JSON</a>
 · <a href="metrics.csv">Every comparison</a></p><h2>Sampled boundary comparison</h2>
<img src="boundary-comparison.png" alt="LOD2 and alternative LOD3 meshes at one sampled boundary condition">
<h2>Selected angles and poses</h2><img src="preview.png" alt="Three representative clips and viewing angles">
<p><a href="front-comparison.png">Compact front comparison</a></p>
<h2>All angles and poses</h2><p>All eight yaws, three clips and selected distinct poses. The full metrics additionally cover
all eight phases, elevations −15°, 0°, +15° and four subpixel alignments. Top-down views are excluded.</p>
<img src="angles-poses.png" alt="All eight yaws and three clips at 80 m">
<p>Capture uses the default 60° vertical FOV at 1280×720, with 4× linear depth masks. Native color has no antialiasing,
world background, fog, shadows or postprocessing. Camera distance is eye-to-aim distance. Results apply to this one
character and camera, not the entire population or every rendering setting.</p><h2>Capture, color ranking and metadata</h2><pre>'''
    document += html.escape(json.dumps({key: value for key, value in payload.items() if key not in ('summary', 'angle_summary', 'clip_summary')}, indent=2))
    (output / 'report.html').write_text(document + '</pre></html>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.samples.resolve().parent
    output = (args.output or root / 'analysis').resolve()
    metadata = json.loads((root / 'metadata.json').read_text())
    groups = load_groups(args.samples, metadata)
    variants = variants_for(next(iter(groups.values())))
    prefixes = prism_prefixes(variants)
    output.mkdir(parents=True, exist_ok=True)
    metrics = []
    for index, group in enumerate(groups.values(), 1):
        metrics.extend(measure_group(root, group))
        if index % 500 == 0:
            print(f'Measured {index}/{len(groups)} paired views', flush=True)
    summary = summaries(metrics)
    visual_sheet(root, groups, output)
    boundary_view = transition_sheet(root, groups, metrics, output)
    scenarios = {'prism': 'zombie-prism-lod3-80m-v1', 'prism_slim': 'zombie-prism-slim-80m-v1',
                 'prism_slim2': 'zombie-prism-slim2-80m-v1'}
    payload = {'scenario': scenarios[prefixes[-1]],
               'variants': variants, 'matched_views': len(groups),
               'comparisons': len(metrics), 'proxy_depth_identical_views': len(groups),
               'color_votes_vs_original': color_votes(metrics), 'boundary_view': boundary_view,
               'color_votes_by_yaw': [
                   {'angle': angle, 'metrics': color_votes([row for row in metrics if row['angle'] == angle])}
                   for angle in sorted({row['angle'] for row in metrics})],
               'capture_metadata': metadata, 'summary': summary}
    if len(prefixes) > 1:
        payload.update(angle_summary=angle_summaries(metrics), slimming_vs_standard=slimming_votes(metrics),
                       proxy_depth_identical_by_geometry={prefix: len(groups) for prefix in prefixes})
        for prefix in prefixes[1:]:
            field = prefix.removeprefix('prism_')
            payload[f'{field}_color_votes_vs_original'] = color_votes(metrics, prefix)
            payload[f'{field}_color_votes_by_yaw'] = [
                {'angle': angle, 'metrics': color_votes([row for row in metrics if row['angle'] == angle], prefix)}
                for angle in sorted({row['angle'] for row in metrics})]
        write_csv(output / 'angle-summary.csv', payload['angle_summary'])
    if prefixes[-1] == 'prism_slim2':
        payload.update(clip_summary=dimension_summaries(metrics, 'clip'),
                       slim2_vs_slim1=geometry_votes(metrics, 'prism_slim', 'prism_slim2',
                                                    ('angle', 'clip', 'elevation')))
        write_csv(output / 'clip-summary.csv', payload['clip_summary'])
    write_csv(output / 'metrics.csv', metrics)
    write_csv(output / 'summary.csv', summary)
    (output / 'summary.json').write_text(json.dumps(payload, indent=2) + '\n')
    write_report(output, payload)
    print(f'Wrote {len(metrics)} comparisons, {len(summary)} summaries to {output}', flush=True)


if __name__ == '__main__':
    main()
