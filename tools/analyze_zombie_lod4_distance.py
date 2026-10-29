#!/usr/bin/env python3
"""Measure a prism LOD4 switch across distance, camera elevation and pixel phase.

The exploratory screen-pixel limits are fixed here before capture analysis.
They describe geometric differences, not human detection or acceptance rates.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
import math
from collections import defaultdict
from itertools import product
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from analyze_zombie_lod_quality import compare, font, read_sample, write_csv


KEYS = ('asset', 'clip', 'phase', 'angle', 'distance', 'elevation',
        'pixel_offset_x', 'pixel_offset_y', 'pixel_phase')
VARIANTS = ('original', 'lod3', 'prism')
PAIRS = (('lod3', 'prism'), ('original', 'lod3'), ('original', 'prism'))
LIMITS = {'edge_p95_across_views_p95_px': 0.5,
          'edge_max_across_views_p99_px': 1.0,
          'newly_invisible_views': 0}
LABELS = {'original': 'Original', 'lod3': 'Current LOD3', 'prism': 'White-vest prism'}


def load_groups(path):
    groups = defaultdict(dict)
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            for field in ('phase', 'angle', 'distance', 'elevation', 'pixel_offset_x', 'pixel_offset_y'):
                row[field] = float(row[field])
            for field in ('triangles', 'vertices', 'width', 'height', 'crop_x', 'crop_y', 'mask_scale', 'pixel_phase'):
                row[field] = int(row[field])
            key = tuple(row[field] for field in KEYS)
            if row['variant'] in groups[key]:
                raise ValueError(f'Duplicate render: {key}, {row["variant"]}')
            groups[key][row['variant']] = row
    if not groups:
        raise ValueError('No captures')
    paired = ('width', 'height', 'crop_x', 'crop_y', 'mask_scale', 'animation_frame', 'animation_hz')
    for key, group in groups.items():
        if set(group) != set(VARIANTS):
            raise ValueError(f'Expected original/lod3/prism for {key}, got {list(group)}')
        for field in paired:
            if len({row[field] for row in group.values()}) != 1:
                raise ValueError(f'Unmatched {field}: {key}')
    return dict(groups)


def validate_completeness(groups, metadata):
    # Capture CSV uses the C++ stream's default six significant digits.
    def number(value):
        return float(f'{value:.6g}')

    expected = set()
    for asset, clip, pose, yaw, distance, elevation, pixel in product(
            {key[0] for key in groups}, ('idle', 'walk', 'run'),
            range(metadata['poses_per_clip']), range(metadata['angles']),
            metadata['distances_m'], metadata['elevations_degrees'],
            enumerate(metadata['pixel_offsets'])):
        pixel_phase, (offset_x, offset_y) = pixel
        expected.add((asset, clip, number(pose / metadata['poses_per_clip']),
                      number(yaw * 360 / metadata['angles']), number(distance),
                      number(elevation), number(offset_x), number(offset_y), pixel_phase))
    actual = set(groups)
    if actual != expected:
        raise ValueError(f'Incomplete capture product: {len(expected - actual)} missing, '
                         f'{len(actual - expected)} unexpected paired views')
    if metadata['samples'] != len(groups) * len(VARIANTS):
        raise ValueError('Capture sample count does not match complete paired views')
    dimensions = {'width': metadata['crop_size'], 'height': metadata['crop_size'],
                  'crop_x': metadata['crop_x'], 'crop_y': metadata['crop_y'],
                  'mask_scale': metadata['mask_scale']}
    for group in groups.values():
        if any(group['original'][field] != value for field, value in dimensions.items()):
            raise ValueError('Capture geometry does not match metadata')


def measure_group(root, group):
    samples = {variant: read_sample(root, group[variant]) for variant in VARIANTS}
    rows = []
    for reference, candidate in PAIRS:
        source, target = samples[reference], samples[candidate]
        row = {key: group[candidate][key] for key in KEYS}
        row.update(reference=reference, candidate=candidate,
                   comparison=f'{candidate}_vs_{reference}',
                   triangles=group[candidate]['triangles'], vertices=group[candidate]['vertices'])
        row.update(compare(source, target, group[candidate]['mask_scale']))
        color_union = source['color_mask'] | target['color_mask']
        different = np.any(np.abs(source['color'].astype(np.int16) - target['color']) > 1, axis=2)
        row.update(native_changed_pixels=int((different & color_union).sum()),
                   native_foreground_union_pixels=int(color_union.sum()),
                   native_missing_pixels=int((source['color_mask'] & ~target['color_mask']).sum()),
                   reference_native_pixels=int(source['color_mask'].sum()),
                   candidate_native_pixels=int(target['color_mask'].sum()),
                   reference_height_px=float(source['bbox'][3] - source['bbox'][1]),
                   candidate_height_px=float(target['bbox'][3] - target['bbox'][1]))
        rows.append(row)
    return rows


def quantile(rows, field, percentile):
    values = [row[field] for row in rows if row[field] is not None]
    return float(np.percentile(values, percentile)) if values else None


def summarize(rows, elevation):
    result = {'distance': rows[0]['distance'], 'elevation': elevation,
              'comparison': rows[0]['comparison'], 'views': len(rows),
              'triangles': rows[0]['triangles'], 'vertices': rows[0]['vertices']}
    for field in ('boundary_p95_px', 'boundary_max_px', 'silhouette_iou',
                  'rgb_rmse_union', 'native_changed_pixels', 'native_missing_pixels',
                  'reference_native_pixels', 'candidate_native_pixels',
                  'reference_height_px', 'candidate_height_px', 'silhouette_area_ratio',
                  'native_color_coverage_ratio', 'native_color_missing_fraction'):
        for percentile, suffix in ((50, 'median'), (95, 'p95'), (99, 'p99'), (100, 'max')):
            result[f'{field}_{suffix}'] = quantile(rows, field, percentile)
    result['reference_invisible_views'] = sum(row['reference_native_pixels'] == 0 for row in rows)
    result['candidate_invisible_views'] = sum(row['candidate_native_pixels'] == 0 for row in rows)
    result['newly_invisible_views'] = sum(row['reference_native_pixels'] > 0 and row['candidate_native_pixels'] == 0
                                          for row in rows)
    result['exploratory_geometry_pass'] = (
        result['boundary_p95_px_p95'] <= LIMITS['edge_p95_across_views_p95_px'] and
        result['boundary_max_px_p99'] <= LIMITS['edge_max_across_views_p99_px'] and
        result['newly_invisible_views'] == 0)
    return result


def summaries(metrics):
    pooled, elevated = defaultdict(list), defaultdict(list)
    for row in metrics:
        pooled[(row['distance'], row['comparison'])].append(row)
        elevated[(row['distance'], row['comparison'], row['elevation'])].append(row)
    return ([summarize(rows, 'all') for _, rows in sorted(pooled.items())] +
            [summarize(rows, key[2]) for key, rows in sorted(elevated.items())])


def stable_threshold(summary):
    # Require every tested elevation as well as the pooled population. A passing
    # isolated distance cannot establish a stable switch in the presence of aliasing.
    distances = sorted({row['distance'] for row in summary})
    passing = {distance: all(row['exploratory_geometry_pass'] for row in summary
                            if row['distance'] == distance and row['comparison'] == 'prism_vs_lod3')
               for distance in distances}
    for distance in distances:
        if all(passing[value] for value in distances if value >= distance):
            return distance
    return None


def roi_for(samples, margin=3):
    boxes = [sample['bbox'] for sample in samples.values()]
    height, width = next(iter(samples.values()))['color'].shape[:2]
    return (max(0, math.floor(min(box[0] for box in boxes)) - margin),
            max(0, math.floor(min(box[1] for box in boxes)) - margin),
            min(width, math.ceil(max(box[2] for box in boxes)) + margin),
            min(height, math.ceil(max(box[3] for box in boxes)) + margin))


def overview(root, groups, output):
    distances = sorted({key[4] for key in groups})
    selected = []
    for distance in distances:
        choices = [key for key in groups if key[4] == distance]
        key = min(choices, key=lambda key: (abs(key[5]), abs(key[3] - 45),
                                          key[1] != 'walk', abs(key[2] - .25), key[8]))
        selected.append(key)
    width, row_height = 1000, 360
    sheet = Image.new('RGB', (width, 95 + row_height * len(selected)), '#14202e')
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 14), 'White-vest prism: LOD4 distance comparison', font=font(23), fill='white')
    draw.text((20, 48), '1280×720 scene, 60° vertical FOV. Native pixels above; nearest-neighbor 12× below.',
              font=font(15), fill='#d0d7df')
    for index, key in enumerate(selected):
        group = groups[key]
        samples = {variant: read_sample(root, group[variant]) for variant in VARIANTS}
        roi = roi_for(samples)
        y = 90 + index * row_height
        draw.text((20, y), f'{key[4]:g} m · {key[1]}, phase {key[2]:g}, yaw {key[3]:g}°, elevation {key[5]:g}°',
                  font=font(15), fill='#a5e7d0')
        for column, variant in enumerate(VARIANTS):
            x = 20 + column * 325
            draw.text((x, y + 27), f'{LABELS[variant]} · {group[variant]["triangles"]} triangles',
                      font=font(14), fill='white')
            patch = Image.fromarray(samples[variant]['color']).crop(roi)
            sheet.paste(patch, (x, y + 52))
            sheet.paste(patch.resize((patch.width * 12, patch.height * 12), Image.Resampling.NEAREST),
                        (x, y + 82))
    sheet.save(output / 'overview.png')


def blink(root, groups, metrics, output, distance):
    candidates = [row for row in metrics if row['comparison'] == 'prism_vs_lod3' and row['distance'] == distance]
    chosen = max(candidates, key=lambda row: (row['native_changed_pixels'], row['boundary_p95_px']))
    key = tuple(chosen[field] for field in KEYS)
    group = groups[key]
    samples = {variant: read_sample(root, group[variant]) for variant in VARIANTS}
    roi = roi_for(samples)
    frames = []
    for variant in ('lod3', 'prism'):
        frame = Image.new('RGB', (max(700, 195 + (roi[2] - roi[0]) * 16),
                                  max(470, 155 + (roi[3] - roi[1]) * 16)), '#14202e')
        draw = ImageDraw.Draw(frame)
        draw.text((20, 15), f'{distance:g} m · {LABELS[variant]}', font=font(23), fill='white')
        draw.text((20, 54), f'{key[1]} phase {key[2]:g}, yaw {key[3]:g}°, elevation {key[5]:g}°, pixel phase {key[8]}',
                  font=font(14), fill='#d0d7df')
        draw.text((20, 81), 'Same-pose A/B blink: largest native changed-pixel count at this distance.',
                  font=font(14), fill='#d0d7df')
        draw.text((20, 109), 'Native size', font=font(14), fill='white')
        patch = Image.fromarray(samples[variant]['color']).crop(roi)
        frame.paste(patch, (20, 135))
        draw.text((175, 109), '16× nearest-neighbor inspection', font=font(14), fill='white')
        frame.paste(patch.resize((patch.width * 16, patch.height * 16), Image.Resampling.NEAREST), (175, 135))
        frames.append(frame)
    frames[0].save(output / 'switch-blink.gif', save_all=True, append_images=frames[1:],
                   duration=650, loop=0, optimize=False, disposal=2)
    frames[0].save(output / 'switch-lod3.png')
    frames[1].save(output / 'switch-prism.png')
    return {field: chosen[field] for field in KEYS} | {
        field: chosen[field] for field in ('native_changed_pixels', 'boundary_p95_px', 'boundary_max_px', 'rgb_rmse_union')}


def write_report(output, payload):
    threshold = payload['stable_tested_threshold_m']
    decision = (f'First stable tested geometric threshold: {threshold:g} m.' if threshold is not None else
                'No stable threshold passed all tested farther distances and elevations.')
    headers = ('Distance (m)', 'LOD3 height (px, median)', 'Edge p95 → across-view p95 (px)',
               'Max edge → across-view p99 (px)', 'Changed native pixels p95', 'RGB RMSE p95',
               'Prism / LOD3 color coverage (median)', 'Missing LOD3 color fraction (p95)',
               'Newly invisible', 'Geometry budget')
    rows = []
    for row in payload['summary']:
        if row['comparison'] != 'prism_vs_lod3' or row['elevation'] != 'all':
            continue
        values = (f'{row["distance"]:g}', f'{row["reference_height_px_median"]:.2f}',
                  f'{row["boundary_p95_px_p95"]:.3f}', f'{row["boundary_max_px_p99"]:.3f}',
                  f'{row["native_changed_pixels_p95"]:.1f}', f'{row["rgb_rmse_union_p95"]:.3f}',
                  f'{row["native_color_coverage_ratio_median"]:.2f}',
                  f'{row["native_color_missing_fraction_p95"]:.2f}',
                  str(row['newly_invisible_views']), 'Pass' if row['exploratory_geometry_pass'] else 'Fail')
        rows.append('<tr>' + ''.join(f'<td>{value}</td>' for value in values) + '</tr>')
    document = '''<!doctype html><html><meta charset="utf-8"><title>Zombie LOD4 distance benchmark</title>
<style>body{font:16px system-ui;background:#14202e;color:#edf3f8;margin:30px;max-width:1300px}a{color:#9bd9ff}
table{border-collapse:collapse;font-size:14px}th,td{padding:10px;border:1px solid #586271;text-align:right}
img{max-width:100%;height:auto}pre{white-space:pre-wrap}p{line-height:1.5}</style>
<h1>White-vest prism: LOD4 distance benchmark</h1>'''
    document += f'<p><b>{decision}</b></p>'
    document += '''<p>One ZombieMale_C model: 60 triangles / 60 vertices in the prism candidate.
Original and current LOD3 remain reference assets. All captures use identical stepped animation frames.
These exploratory geometric limits are not a human-perception validation. RGB changes are reported,
but the geometric pass does not certify color or shading transitions.</p>
<p>Criteria fixed before analysis: across-view 95th percentile of per-view symmetric boundary p95 ≤0.5 native
pixels; across-view 99th percentile of maximum boundary error ≤1 pixel; no newly invisible native-color views.
The stable threshold also requires every sampled elevation and every farther tested distance to pass.
Native color visibility means any RGB channel differs from the background by more than one byte.
Coverage ratio and missing-color fraction expose broadening or lost native pixels even when geometric limits pass.
RGB RMSE uses sRGB byte values normalized to 0–1; it is not a perceptual color metric.</p>
<p>Default gameplay draw cutoff is 180 m. Results beyond 180 m are useful with extended/unlimited draw distance.
Capture resolution is 1280×720 with 60° vertical FOV; other projections/resolutions require screen-size scaling
and their own rasterization checks. No fog, shadows, world background, antialiasing or postprocessing.</p>'''
    document += '''<p>Eight sampled poses per clip do not test continuous movement. Elevations −15°, 0° and +15°
exclude top-down views through the open prism ends. Camera distance is measured to the fixed aim point,
not separately to every body part. Current LOD3 is forced as the switch reference at the 80 m boundary.</p>'''
    document += '<table><tr>' + ''.join(f'<th>{value}</th>' for value in headers) + '</tr>' + ''.join(rows) + '</table>'
    document += '''<p><a href="summary.csv">Pooled and elevation summaries</a> · <a href="summary.json">Full JSON</a>
 · <a href="metrics.csv">Every matched comparison</a></p>
<h2>Native pixels and enlarged comparison</h2><p>Same view chosen at every distance; enlargement makes raster and shading differences inspectable.</p>
<img src="overview.png" alt="Original, LOD3 and white-vest prism across distance">
<h2>Deliberate switch inspection</h2><p>Same-pose A/B blink, selected by largest changed native-pixel count at the illustrated distance.
This is a visual inspection aid, not continuous animation, an approach sequence, or a user detection study.</p>
<img src="switch-blink.gif" alt="Current LOD3 and prism A/B switch">
<h2>Capture and analysis metadata</h2><pre>'''
    document += html.escape(json.dumps({key: value for key, value in payload.items() if key != 'summary'}, indent=2))
    document += '</pre></html>\n'
    (output / 'report.html').write_text(document)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.samples.resolve().parent
    output = (args.output or root / 'analysis').resolve()
    groups = load_groups(args.samples)
    metadata = json.loads((root / 'metadata.json').read_text())
    validate_completeness(groups, metadata)
    output.mkdir(parents=True, exist_ok=True)
    metrics = []
    for index, group in enumerate(groups.values(), 1):
        metrics.extend(measure_group(root, group))
        if index % 1000 == 0:
            print(f'Measured {index}/{len(groups)} paired views', flush=True)
    summary = summaries(metrics)
    threshold = stable_threshold(summary)
    illustration = threshold if threshold is not None else min({row['distance'] for row in metrics}, key=lambda value: abs(value - 180))
    overview(root, groups, output)
    blink_view = blink(root, groups, metrics, output, illustration)
    payload = {'scenario': 'zombie-lod4-distance-v1', 'matched_views': len(groups),
               'comparisons': len(metrics), 'criteria': LIMITS,
               'criteria_note': 'Exploratory geometric budget; no color/shading pass or perceptual certification',
               'stable_tested_threshold_m': threshold, 'blink_view': blink_view,
               'capture_metadata': metadata, 'summary': summary}
    write_csv(output / 'metrics.csv', metrics)
    write_csv(output / 'summary.csv', summary)
    (output / 'summary.json').write_text(json.dumps(payload, indent=2) + '\n')
    write_report(output, payload)
    print(f'Wrote {len(metrics)} comparisons, {len(summary)} summaries; stable threshold={threshold}; {output}', flush=True)


if __name__ == '__main__':
    main()
