#!/usr/bin/env python3
"""Measure paired zombie renders and create a local visual comparison report.

Input is samples.csv from benchmark_zombie_lod_quality. Each reference/candidate
pair must have the same pose, camera, crop, color resolution and mask scale.
Depth masks may be supersampled; all reported distances are native scene pixels.
The metrics describe differences, not a perceptual pass/fail certification.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import binary_erosion
from scipy.spatial import cKDTree


KEYS = ('asset', 'clip', 'phase', 'angle', 'distance')
VARIANTS = ('original', 'lod1', 'candidate35', 'candidate25', 'lod2',
            'candidate15', 'candidate10', 'lod3', 'candidate5', 'prism', 'candidate3', 'candidate2')
LABELS = {'original': 'Original · 100%', 'lod1': 'Current LOD1 · ≈50%',
          'lod2': 'Current LOD2 · ≈20%', 'lod3': 'Current LOD3',
          'candidate35': 'Candidate · 35%', 'candidate25': 'Candidate · 25%',
          'candidate15': 'Candidate · 15%', 'candidate10': 'Candidate · 10%',
          'candidate5': 'Candidate · 5%', 'candidate3': 'Candidate · 3%',
          'candidate2': 'Candidate · 2%', 'prism': 'Open triangular prisms · 60 tris'}
MEASURES = ('silhouette_iou', 'boundary_mean_px', 'boundary_p95_px',
            'boundary_max_px', 'rgb_rmse_union', 'rgb_rmse_intersection',
            'changed_fraction_union', 'silhouette_area_ratio',
            'native_color_coverage_ratio', 'native_color_missing_fraction')


def current_lod(row):
    if row.get('current_lod'):
        return row['current_lod']
    distance = row['distance']
    return 'lod1' if distance < 35 else 'lod2' if distance < 80 else 'lod3'


def display_zoom(distance):
    return 2 if distance < 35 else 4 if distance < 80 else 8 if distance < 150 else 12


def load_rows(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    required = set(KEYS) | {'variant', 'triangles', 'vertices', 'width', 'height',
                            'crop_x', 'crop_y', 'image', 'mask'}
    if not rows or not required.issubset(rows[0]):
        raise ValueError('samples.csv is empty or missing required columns')
    groups = defaultdict(dict)
    for row in rows:
        for name in ('phase', 'angle', 'distance'):
            row[name] = float(row[name])
        for name in ('triangles', 'vertices', 'width', 'height', 'crop_x', 'crop_y'):
            row[name] = int(row[name])
        row['mask_scale'] = int(row.get('mask_scale') or 1)
        key = tuple(row[name] for name in KEYS)
        if row['variant'] in groups[key]:
            raise ValueError(f'Duplicate render: {key}, {row["variant"]}')
        groups[key][row['variant']] = row
    for key, group in groups.items():
        if 'original' not in group:
            raise ValueError(f'Missing original reference: {key}')
        geometry = tuple(group['original'][name] for name in
                         ('width', 'height', 'crop_x', 'crop_y', 'mask_scale'))
        if any(tuple(row[name] for name in
                     ('width', 'height', 'crop_x', 'crop_y', 'mask_scale')) != geometry
               for row in group.values()):
            raise ValueError(f'Inconsistent paired capture geometry: {key}')
        if len({current_lod(row) for row in group.values()}) != 1:
            raise ValueError(f'Inconsistent paired current LOD: {key}')
        if 'animation_frame' in group['original']:
            reference_frame = float(group['original']['animation_frame'])
            if any(abs(float(row['animation_frame']) - reference_frame) > 1e-6
                   for row in group.values()):
                raise ValueError(f'Inconsistent paired animation frame: {key}')
    return rows, groups


def sample_paths(root, row):
    return tuple((root / row[name]).resolve() for name in ('image', 'mask'))


def read_sample(root, row):
    image_path, mask_path = sample_paths(root, row)
    with Image.open(image_path) as source:
        color = np.asarray(source.convert('RGB')).copy()
    with Image.open(mask_path) as source:
        mask_image = source.convert('L')
        bounds = mask_image.getbbox()
        mask_size = mask_image.size
        mask = None if bounds is None else np.asarray(mask_image.crop(bounds)) > 0
    height, width, scale = row['height'], row['width'], row['mask_scale']
    if color.shape != (height, width, 3) or mask_size != (width * scale, height * scale):
        raise ValueError(f'Wrong capture dimensions: {image_path}')
    if bounds is None:
        raise ValueError(f'Empty foreground mask: {mask_path}')
    left, top, right, bottom = bounds
    # An area>0 native pixel mask includes every pixel touched by foreground at
    # supersampled resolution. Color metrics are evaluated only on these pixels.
    # Keep the high-resolution mask cropped: far actors cover only a small part
    # of the image. Global origins preserve exact intersection/edge coordinates.
    native_mask = np.zeros((height, width), dtype=bool)
    ys, xs = np.nonzero(mask)
    native_mask[(ys + top) // scale, (xs + left) // scale] = True
    edge = mask & ~binary_erosion(mask, structure=np.ones((3, 3)), border_value=0)
    ey, ex = np.nonzero(edge)
    boundary = np.column_stack((ex + left, ey + top)).astype(np.float64)
    color_mask = np.any(np.abs(color.astype(np.int16) - color[0, 0].astype(np.int16)) > 1, axis=2)
    return {'color': color, 'mask': mask, 'native_mask': native_mask,
            'edge': boundary, 'tree': cKDTree(boundary),
            'bounds': bounds, 'area': int(mask.sum()), 'color_mask': color_mask,
            'bbox': (left / scale, top / scale, right / scale, bottom / scale)}


def compare(reference, candidate, scale):
    if reference is candidate:
        return {'silhouette_iou': 1., 'boundary_mean_px': 0., 'boundary_p95_px': 0.,
                'boundary_max_px': 0., 'rgb_rmse_union': 0., 'rgb_rmse_intersection': 0.,
                'changed_fraction_union': 0., 'silhouette_area_ratio': 1.,
                'native_color_coverage_ratio': 1. if reference['color_mask'].any() else None,
                'native_color_missing_fraction': 0. if reference['color_mask'].any() else None}
    left = max(reference['bounds'][0], candidate['bounds'][0])
    top = max(reference['bounds'][1], candidate['bounds'][1])
    right = min(reference['bounds'][2], candidate['bounds'][2])
    bottom = min(reference['bounds'][3], candidate['bounds'][3])
    intersection = 0
    if left < right and top < bottom:
        def overlap(sample):
            x, y = sample['bounds'][:2]
            return sample['mask'][top - y:bottom - y, left - x:right - x]
        intersection = int((overlap(reference) & overlap(candidate)).sum())
    union = reference['area'] + candidate['area'] - intersection
    distances = np.concatenate((
        reference['tree'].query(candidate['edge'])[0],
        candidate['tree'].query(reference['edge'])[0])) / scale
    color_union = reference['native_mask'] | candidate['native_mask']
    color_intersection = reference['native_mask'] & candidate['native_mask']
    byte_difference = reference['color'].astype(np.int16) - candidate['color']
    difference = byte_difference.astype(np.float32) / 255

    def rmse(mask):
        return float(np.sqrt(np.mean(difference[mask] ** 2))) if mask.any() else None

    return {
        'silhouette_iou': float(intersection / union),
        'boundary_mean_px': float(distances.mean()),
        'boundary_p95_px': float(np.percentile(distances, 95)),
        'boundary_max_px': float(distances.max()),
        'rgb_rmse_union': rmse(color_union),
        'rgb_rmse_intersection': rmse(color_intersection),
        'changed_fraction_union': float(np.any(np.abs(byte_difference[color_union]) > 1,
                                               axis=1).mean()),
        'silhouette_area_ratio': float(candidate['area'] / reference['area']),
        'native_color_coverage_ratio': (float(candidate['color_mask'].sum() / reference['color_mask'].sum())
                                        if reference['color_mask'].any() else None),
        'native_color_missing_fraction': (float((reference['color_mask'] & ~candidate['color_mask']).sum() /
                                               reference['color_mask'].sum())
                                           if reference['color_mask'].any() else None),
    }


def write_csv(path, rows):
    if not rows:
        return
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def image_for_browser(path, output):
    if path.suffix.lower() == '.png':
        return Path(os.path.relpath(path, output)).as_posix()
    target = output / 'images' / (hashlib.sha256(str(path).encode()).hexdigest()[:20] + '.png')
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(path) as source:
            source.save(target)
    return target.relative_to(output).as_posix()


def summaries(metrics):
    grouped = defaultdict(list)
    for row in metrics:
        grouped[(row['distance'], row['variant'])].append(row)
    output = []
    for (distance, variant), rows in sorted(grouped.items(), key=lambda item:
            (item[0][0], VARIANTS.index(item[0][1]) if item[0][1] in VARIANTS else len(VARIANTS))):
        result = {'distance': distance, 'variant': variant, 'samples': len(rows),
                  'triangles_mean': float(np.mean([row['triangles'] for row in rows])),
                  'triangle_ratio_mean': float(np.mean([row['triangle_ratio'] for row in rows])),
                  'zero_native_color_views': sum(row['native_color_pixels'] == 0 for row in rows)}
        for name in MEASURES:
            values = [row[name] for row in rows if row[name] is not None]
            for statistic, operation in (('median', np.median), ('p95', lambda x: np.percentile(x, 95)),
                                         ('min', np.min), ('max', np.max)):
                result[f'{name}_{statistic}'] = float(operation(values)) if values else None
        worst = max(rows, key=lambda row: row['boundary_p95_px'])
        result['worst_p95_view'] = ' / '.join(str(worst[name]) for name in KEYS)
        output.append(result)
    return output


def font(size):
    try:
        return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', size)
    except OSError:
        return ImageFont.load_default()


def contact_sheets(root, groups, measured, output):
    by_subject = defaultdict(list)
    for key in groups:
        by_subject[(key[0], key[4])].append(key)
    paths = []
    for (asset, distance), keys in sorted(by_subject.items()):
        # Deliberately show several motions, angles and phases, plus the view
        # with the largest current-LOD 95th percentile boundary displacement.
        current = current_lod(groups[keys[0]]['original'])
        desired = (('idle', 0, 0), ('walk', .25, 45), ('walk', .5, 90),
                   ('run', .25, 180), ('run', .75, 270))
        selected = []
        for clip, phase, angle in desired:
            eligible = [key for key in keys if key[1] == clip]
            if eligible:
                chosen = min(eligible, key=lambda key: abs(key[2] - phase) +
                             abs(((key[3] - angle + 180) % 360) - 180) / 360)
                if chosen not in selected:
                    selected.append(chosen)
        eligible = [key for key in keys if (key, current) in measured]
        if eligible:
            worst = max(eligible, key=lambda key: measured[(key, current)]['boundary_p95_px'])
            if worst not in selected:
                selected.append(worst)
        prism_keys = [key for key in keys if (key, 'prism') in measured]
        if prism_keys:
            worst = max(prism_keys, key=lambda key: measured[(key, 'prism')]['boundary_p95_px'])
            if worst not in selected:
                selected.append(worst)
        variants = [variant for variant in VARIANTS if any(variant in groups[key] for key in selected)]
        zoom = display_zoom(distance)
        loaded = {key: {variant: read_sample(root, row) for variant, row in groups[key].items()}
                  for key in selected}
        boxes = [sample['bbox'] for group in loaded.values() for sample in group.values()]
        roi = (max(0, math.floor(min(box[0] for box in boxes)) - 5),
               max(0, math.floor(min(box[1] for box in boxes)) - 5),
               min(groups[selected[0]]['original']['width'], math.ceil(max(box[2] for box in boxes)) + 5),
               min(groups[selected[0]]['original']['height'], math.ceil(max(box[3] for box in boxes)) + 5))
        roi_width, roi_height = roi[2] - roi[0], roi[3] - roi[1]
        cell_width = max(150, roi_width * (zoom + 1) + 20)
        row_height = max(roi_height * zoom, roi_height) + 52
        sheet = Image.new('RGB', (cell_width * len(variants), 94 + row_height * len(selected)), '#18202a')
        draw = ImageDraw.Draw(sheet)
        draw.text((12, 10), f'{asset} | {distance:g} m | native + {zoom}x nearest-neighbor',
                  fill='white', font=font(18))
        draw.text((12, 37), 'Same camera and pose in each row. Border metrics use supersampled depth masks.',
                  fill='#cbd5e1', font=font(13))
        for column, variant in enumerate(variants):
            draw.text((column * cell_width + 8, 65), LABELS.get(variant, variant),
                      fill='#8ee6c0' if variant == current else 'white', font=font(13))
            if variant == current:
                draw.text((column * cell_width + 8, 81), 'Selected at this distance',
                          fill='#8ee6c0', font=font(10))
        for index, key in enumerate(selected):
            y = 94 + index * row_height
            for column, variant in enumerate(variants):
                if variant not in loaded[key]:
                    continue
                x = column * cell_width + 8
                sample = loaded[key][variant]
                patch = Image.fromarray(sample['color']).crop(roi)
                sheet.paste(patch, (x, y))
                sheet.paste(patch.resize((roi_width * zoom, roi_height * zoom), Image.Resampling.NEAREST),
                            (x + roi_width + 7, y))
                data = measured.get((key, variant))
                label_y = y + roi_height * zoom + 3
                draw.text((x, label_y), f'{key[1]} phase {key[2]:g}, angle {key[3]:g}',
                          fill='#cbd5e1', font=font(11))
                if data:
                    draw.text((x, label_y + 17), f'{data["triangles"]} tris; edge p95 {data["boundary_p95_px"]:.2f}px',
                              fill='white', font=font(11))
        target = output / 'contact-sheets' / f'{Path(asset).stem}-{distance:g}m.png'
        target.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(target)
        paths.append({'asset': asset, 'distance': distance, 'path': target.relative_to(output).as_posix()})
    return paths


def overview_sheet(root, groups, measured, output):
    selections = []
    subjects = sorted({key[0] for key in groups})
    for distance in sorted({key[4] for key in groups}):
        first = next(group['original'] for key, group in groups.items() if key[4] == distance)
        current = current_lod(first)
        distance_groups = [group for key, group in groups.items() if key[4] == distance]
        if any('prism' in group for group in distance_groups):
            variants = ('original', current, 'prism')
            if all('candidate3' in group for group in distance_groups):
                variants += ('candidate3',)
            worst_variant = 'prism'
        else:
            moderate, aggressive = {'lod1': ('candidate35', 'candidate25'),
                                    'lod2': ('candidate15', 'candidate10'),
                                    'lod3': ('candidate5', 'candidate3')}[current]
            variants = (('original', 'candidate10', current, moderate, aggressive) if current == 'lod3'
                        else ('original', current, moderate, aggressive))
            worst_variant = aggressive if current == 'lod1' else moderate
        eligible = [key for key in groups if key[4] == distance and all(v in groups[key] for v in variants)]
        if not eligible:
            continue
        for asset, clip, phase, angle in (('ZombieFemale_A.glb', 'walk', .25, 45),
                                          ('ZombieMale_C.glb', 'run', .5, 90)):
            wanted = [key for key in eligible if key[0] == asset and key[1] == clip]
            if not wanted:
                wanted = [key for key in eligible if key[0] == subjects[len(selections) % len(subjects)]] or eligible
            key = min(wanted, key=lambda key: abs(key[2] - phase) +
                      abs(((key[3] - angle + 180) % 360) - 180) / 360)
            selections.append((key, variants, 'Representative view'))
        worst = max(eligible, key=lambda key: measured[(key, worst_variant)]['boundary_p95_px'])
        selections.append((worst, variants, f'Largest edge p95 for {LABELS[worst_variant]}'))
    if not selections:
        return None
    cell_width, margin = 260, 12
    width = cell_width * max(len(variants) for _, variants, _ in selections)
    rows = []
    for key, variants, reason in selections:
        samples = {variant: read_sample(root, groups[key][variant]) for variant in variants}
        boxes = [sample['bbox'] for sample in samples.values()]
        roi = (max(0, math.floor(min(box[0] for box in boxes)) - 4),
               max(0, math.floor(min(box[1] for box in boxes)) - 4),
               min(groups[key]['original']['width'], math.ceil(max(box[2] for box in boxes)) + 4),
               min(groups[key]['original']['height'], math.ceil(max(box[3] for box in boxes)) + 4))
        zoom = display_zoom(key[4])
        rows.append((key, variants, reason, samples, roi, zoom, (roi[3] - roi[1]) * (zoom + 1) + 104))
    sheet = Image.new('RGB', (width, 95 + sum(row[-1] for row in rows)), '#18202a')
    draw = ImageDraw.Draw(sheet)
    levels = sorted({current_lod(group['original']) for group in groups.values()})
    title = ' / '.join(level.upper() for level in levels)
    heading = ('Zombie prototype: open triangular prisms' if any('prism' in group for group in groups.values())
               else f'Zombie {title}: original and progressively cheaper meshes')
    draw.text((margin, 12), heading, font=font(21), fill='white')
    draw.text((margin, 43), 'Each cell: native-size color above, nearest-neighbor enlargement below. Pose and camera are matched.',
              font=font(13), fill='#cbd5e1')
    draw.text((margin, 64), 'All figures are differences from the original mesh; edge p95 is measured in native scene pixels.',
              font=font(13), fill='#cbd5e1')
    y = 95
    for key, variants, reason, samples, roi, zoom, row_height in rows:
        draw.line((0, y, width, y), fill='#405368')
        draw.text((margin, y + 7), f'{key[4]:g} m | {key[0]} | {key[1]} phase {key[2]:g}, angle {key[3]:g}° | {reason}',
                  font=font(12), fill='white')
        roi_width, roi_height = roi[2] - roi[0], roi[3] - roi[1]
        for column, variant in enumerate(variants):
            x = column * cell_width + margin
            draw.text((x, y + 30), LABELS[variant], font=font(14),
                      fill='#8ee6c0' if variant == current_lod(groups[key]['original']) else 'white')
            patch = Image.fromarray(samples[variant]['color']).crop(roi)
            sheet.paste(patch, (x, y + 53))
            sheet.paste(patch.resize((roi_width * zoom, roi_height * zoom), Image.Resampling.NEAREST),
                        (x, y + 63 + roi_height))
            measure = measured[(key, variant)]
            draw.text((x, y + row_height - 26), f'{measure["triangles"]} tris | edge p95 {measure["boundary_p95_px"]:.2f}px',
                      font=font(12), fill='white')
        y += row_height
    target = output / 'overview.png'
    sheet.save(target)
    return target.name


HTML = r'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Zombie LOD quality comparison</title>
<style>
:root { color-scheme: dark; font-family: system-ui,sans-serif; background:#101820; color:#e8edf2 }
body { margin:24px; max-width:1800px } h1 { margin-bottom:8px } p { max-width:1100px; line-height:1.5 }
label { display:inline-flex; flex-direction:column; gap:4px; margin:6px } select,button { padding:7px; font:inherit }
.controls { position:sticky; top:0; background:#101820f5; border-bottom:1px solid #475569; z-index:2; padding:8px 0 }
.variants { display:flex; gap:14px; flex-wrap:wrap } .variants label { flex-direction:row }
.grid { display:flex; gap:14px; flex-wrap:wrap; align-items:flex-start; margin-top:16px }
.tile { border:1px solid #405368; padding:10px; background:#182531 } .tile.current { border:2px solid #8ee6c0 }
.tile h3 { margin:0 0 6px } .tile img { display:block; image-rendering:pixelated; background:#808080 }
.tile p { font-size:13px; margin:6px 0; max-width:250px } .muted { color:#b6c5d4 }
table { border-collapse:collapse; font-size:13px } td,th { padding:8px; border:1px solid #405368; text-align:right }
td:nth-child(2),th:nth-child(2) { text-align:left } a { color:#a5d8ff } details { margin:18px 0 } code { color:#b9eedc }
.warning { color:#ffe2a5 } #overview { overflow:auto }
</style>
<h1>Zombie LOD quality comparison</h1>
<p id="description"></p>
<p id="overview-link"></p>
<p class="warning">The captures compare mesh detail at identical poses. These image metrics are descriptive; a 0.5-pixel budget is exploratory, not a perceptual certification. Inspect animated poses, silhouettes, shading and switching at native size.</p>
<p id="prototype-note" class="muted" hidden></p>
<div class="controls"><div id="selectors"></div><div class="variants" id="variants"></div>
<button id="play">Cycle poses</button> <span class="muted">Left/right: viewing angle · Up/down: pose</span></div>
<p id="view"></p><div class="grid" id="grid"></div>
<details open><summary>Aggregate measurements</summary><p class="muted">All angles, phases, clips and characters. Edge p95: 95th percentile of symmetric nearest boundary-pixel distances within each view. Across-view p95 is then computed over those per-view values. RGB RMSE is normalized to 0–1 and covers foreground union only.</p><div id="overview"></div></details>
<details><summary>Contact sheets</summary><div id="sheets"></div></details>
<details><summary>Method and capture metadata</summary><pre id="metadata"></pre></details>
<p><a href="metrics.csv">Every comparison (CSV)</a> · <a href="summary.csv">Aggregate CSV</a> · <a href="summary.json">Aggregate JSON</a></p>
<script id="data" type="application/json">__DATA__</script>
<script>
const data=JSON.parse(document.getElementById('data').textContent), fields=['distance','asset','clip','angle','phase'];
const values=f=>[...new Set(data.views.map(v=>v[f]))].sort((a,b)=>typeof a==='number'?a-b:a.localeCompare(b));
const controls={};
for (const field of fields) {
  const label=document.createElement('label'); label.textContent=field[0].toUpperCase()+field.slice(1);
  const select=document.createElement('select');
  for(const value of values(field)) { const option=document.createElement('option'); option.value=value; option.textContent=value; select.append(option); }
  select.onchange=render; label.append(select); document.getElementById('selectors').append(label); controls[field]=select;
}
for(const [field,options] of [['zoom',['1','2','4','8','12','16']],['display',['color','silhouette']]]) {
  const label=document.createElement('label'); label.textContent=field; const select=document.createElement('select');
  for(const value of options) {const option=document.createElement('option');option.value=value;option.textContent=field==='zoom'?`${value}× pixels`:value;select.append(option);}
  label.append(select);document.getElementById('selectors').append(label);controls[field]=select;select.onchange=render;
}
const shown=new Set(data.variants);
for(const variant of data.variants) {
 const label=document.createElement('label'), input=document.createElement('input');input.type='checkbox';input.checked=true;
 input.onchange=()=>{input.checked?shown.add(variant):shown.delete(variant);render();};label.append(input,document.createTextNode(data.labels[variant]||variant));document.getElementById('variants').append(label);
}
const fmt=(x,d=3)=>x===null?'—':Number(x).toFixed(d);
function render() {
 const view=data.views.find(v=>fields.every(f=>String(v[f])===controls[f].value));
 const grid=document.getElementById('grid');grid.replaceChildren();
 if(!view){document.getElementById('view').textContent='No capture for this combination.';return;}
 document.getElementById('view').textContent=`${view.asset} · ${view.clip} · phase ${view.phase} · angle ${view.angle}° · ${view.distance} m · current policy ${view.current}`;
 for(const variant of data.variants) {
  const item=view.variants[variant];if(!shown.has(variant)||!item)continue;
  const tile=document.createElement('div');tile.className='tile'+(variant===view.current?' current':'');
  const title=document.createElement('h3');title.textContent=(data.labels[variant]||variant)+(variant===view.current?' · selected':'');tile.append(title);
  const image=document.createElement('img');image.src=controls.display.value==='color'?item.image:item.mask;
  image.width=item.width*Number(controls.zoom.value);image.height=item.height*Number(controls.zoom.value);image.alt=`${variant}, ${view.clip}, ${view.angle} degrees`;tile.append(image);
  const text=document.createElement('p');text.textContent=`${item.triangles} triangles / ${item.vertices} vertices`;
  tile.append(text);
  if(item.metrics) {const p=document.createElement('p');p.textContent=`Silhouette IoU ${fmt(item.metrics.silhouette_iou)} · edge p95 ${fmt(item.metrics.boundary_p95_px,2)} px · max ${fmt(item.metrics.boundary_max_px,2)} px · foreground RGB RMSE ${fmt(item.metrics.rgb_rmse_union)} · native color coverage ${fmt(item.metrics.native_color_coverage_ratio*100,1)}% · missing ${fmt(item.metrics.native_color_missing_fraction*100,1)}%`;tile.append(p);}
  grid.append(tile);
 }
}
let timer=null;document.getElementById('play').onclick=()=>{if(timer){clearInterval(timer);timer=null;document.getElementById('play').textContent='Cycle poses';}else{timer=setInterval(()=>step('phase',1),400);document.getElementById('play').textContent='Stop';}};
function step(field,delta){const c=controls[field];c.selectedIndex=(c.selectedIndex+delta+c.options.length)%c.options.length;render();}
document.onkeydown=e=>{if(['INPUT','SELECT'].includes(e.target.tagName))return;if(e.key==='ArrowLeft')step('angle',-1);else if(e.key==='ArrowRight')step('angle',1);else if(e.key==='ArrowUp')step('phase',1);else if(e.key==='ArrowDown')step('phase',-1);else return;e.preventDefault();};
document.getElementById('description').textContent=`${data.views.length.toLocaleString()} matched views, ${data.variants.length} mesh variants. Native colors use the current 720p projection; depth masks use ${data.mask_scales.join('/')}× sampling. Images remain registered to the same camera and crop.`;
if(data.variants.includes('prism')){const note=document.getElementById('prototype-note');note.hidden=false;note.textContent='The open-prism proxy uses approximate per-vertex colors and a different material. Silhouette measurements assess geometry; RGB differences also include this appearance approximation.';}
if(data.overview){const a=document.createElement('a');a.href=data.overview;a.textContent='Open compact comparison overview';document.getElementById('overview-link').append(a);}
const columns=[['distance','m'],['variant','Variant'],['triangles_mean','Mean tris'],['triangle_ratio_mean','Original ratio'],['silhouette_iou_median','Median IoU'],['silhouette_iou_min','Worst IoU'],['boundary_p95_px_median','Median edge p95'],['boundary_p95_px_p95','Across-view p95'],['boundary_max_px_max','Worst edge max'],['rgb_rmse_union_p95','RGB RMSE p95'],['native_color_coverage_ratio_median','Median native coverage ratio'],['native_color_missing_fraction_p95','Native missing fraction p95'],['zero_native_color_views','Zero native-pixel views']];
const table=document.createElement('table'), header=document.createElement('tr');for(const [,title]of columns){const th=document.createElement('th');th.textContent=title;header.append(th);}table.append(header);
for(const row of data.summary){const tr=document.createElement('tr');for(const [key]of columns){const td=document.createElement('td');td.textContent=key==='variant'?(data.labels[row[key]]||row[key]):typeof row[key]==='number'?fmt(row[key],key==='distance'||key==='triangles_mean'?0:3):row[key];tr.append(td);}table.append(tr);}document.getElementById('overview').append(table);
for(const sheet of data.sheets){const p=document.createElement('p'),a=document.createElement('a');a.href=sheet.path;a.textContent=`${sheet.asset}, ${sheet.distance} m`;p.append(a);document.getElementById('sheets').append(p);}
document.getElementById('metadata').textContent=JSON.stringify(data.metadata,null,2);render();
</script></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--no-contact-sheets', action='store_true')
    args = parser.parse_args()
    source = args.samples.resolve()
    root = source.parent
    output = (args.output or root / 'analysis').resolve()
    output.mkdir(parents=True, exist_ok=True)
    rows, groups = load_rows(source)
    metrics, measured, views = [], {}, []
    for index, (key, group) in enumerate(sorted(groups.items())):
        reference = read_sample(root, group['original'])
        current_name = current_lod(group['original'])
        current = read_sample(root, group[current_name]) if current_name in group else None
        current_difference = compare(reference, current, group['original']['mask_scale']) if current is not None else None
        view = dict(zip(KEYS, key)) | {'current': current_name, 'variants': {}}
        for variant, row in group.items():
            candidate = (reference if variant == 'original' else current
                         if variant == current_name and current is not None else read_sample(root, row))
            result = dict(zip(KEYS, key)) | {'variant': variant, 'triangles': row['triangles'],
                     'vertices': row['vertices'], 'triangle_ratio': row['triangles'] / group['original']['triangles'],
                     'reference_height_px': reference['bbox'][3] - reference['bbox'][1],
                     'mask_scale': row['mask_scale'], 'current_lod': current_name,
                     'native_color_pixels': int(candidate['color_mask'].sum()),
                     'reference_native_color_pixels': int(reference['color_mask'].sum())}
            result.update(current_difference if candidate is current else compare(reference, candidate, row['mask_scale']))
            if current is not None:
                current_metrics = (current_difference if candidate is reference else
                                   compare(current, candidate, row['mask_scale']))
                result.update({f'current_{name}': current_metrics[name] for name in
                               ('silhouette_iou', 'boundary_p95_px', 'boundary_max_px', 'rgb_rmse_union')})
            else:
                result.update({f'current_{name}': None for name in
                               ('silhouette_iou', 'boundary_p95_px', 'boundary_max_px', 'rgb_rmse_union')})
            metrics.append(result)
            measured[(key, variant)] = result
            image_path, mask_path = sample_paths(root, row)
            view['variants'][variant] = {'image': image_for_browser(image_path, output),
                'mask': image_for_browser(mask_path, output), 'triangles': row['triangles'],
                'vertices': row['vertices'], 'width': row['width'], 'height': row['height'],
                'metrics': {name: result[name] for name in MEASURES}}
        views.append(view)
        if (index + 1) % 200 == 0:
            print(f'Analyzed {index + 1}/{len(groups)} matched views', flush=True)
    summary = summaries(metrics)
    write_csv(output / 'metrics.csv', metrics)
    write_csv(output / 'summary.csv', summary)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    sheets = [] if args.no_contact_sheets else contact_sheets(root, groups, measured, output)
    overview = None if args.no_contact_sheets else overview_sheet(root, groups, measured, output)
    metadata_path = root / 'metadata.json'
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else {}
    metadata['analysis'] = {'samples_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'matched_views': len(groups), 'renders': len(rows), 'alignment': 'same camera and crop; no registration',
        'boundary': '8-connected foreground boundary; symmetric Euclidean nearest boundary distance in native pixels',
        'color': 'native RGB RMSE /255; high-resolution masks max-pooled to touched native pixels',
        'native_color_coverage': 'Pixels differing by more than one byte from the flat corner background; missing fraction is relative to original color coverage',
        'quality_budget': '0.5 pixels is exploratory, not a perceptual guarantee',
        'subpixel_sampling': 'mask sample locations limit precision; not an analytic Hausdorff bound'}
    if any('prism' in group for group in groups.values()):
        metadata['analysis']['proxy_appearance'] = (
            'Prism uses approximate per-vertex colors and a different material; '
            'RGB differences include appearance approximation. Silhouette metrics assess geometry.')
    payload = {'views': views, 'summary': summary, 'sheets': sheets, 'overview': overview,
               'metadata': metadata, 'labels': LABELS,
               'variants': [variant for variant in VARIANTS if any(row['variant'] == variant for row in rows)],
               'mask_scales': sorted({row['mask_scale'] for row in rows})}
    encoded = json.dumps(payload, separators=(',', ':')).replace('<', '\\u003c')
    (output / 'report.html').write_text(HTML.replace('__DATA__', encoded))
    print(f'Wrote {len(metrics)} comparisons, {len(summary)} summaries and {len(sheets)} contact sheets to {output}')


if __name__ == '__main__':
    main()
