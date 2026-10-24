#!/usr/bin/env python3
"""Generate four vehicle LoDs, texture inventory and matching previews offline."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.glb import read_glb
from src.vehicle_lod import chassis_model, generate_levels, texture_inventory
from src.vehicle_asset_render import geometry, render, reference_for
from tools.download_vehicle_models import atomic, json_bytes
from tools.prepare_vehicle_models import image_bytes, font


def write_report(report, root):
    rows = report['variants']
    source = sum(v['source_textures']['gpu_bytes'] for v in rows) / 2**20
    levels = [sum(v['levels'][i]['textures']['gpu_bytes'] for v in rows) / 2**20 for i in range(4)]
    text = ["# Vehicle LoDs and texture inventory", "",
        "Generated offline from the selected prepared meshes. Counts include active scene instances. "
        "GPU estimates match the current uploader: uncompressed RGBA8/sRGB8-alpha8 plus every mip level, "
        "one allocation per glTF texture object. They exclude meshes, framebuffer memory and driver overhead.", "",
        "| Vehicle | Source texture dimensions | Source GPU MiB | LOD0 | LOD1 | LOD2 | LOD3 |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: |"]
    for row in rows:
        sizes = ', '.join(str(i['width'])+'×'+str(i['height']) for i in row['source_textures']['images']) or 'none'
        counts = ' | '.join(str(l['triangles']) for l in row['levels'])
        text.append(f"| {row['name']} | {sizes} | {row['source_textures']['gpu_bytes']/2**20:.2f} | {counts} |")
    text += ['', f"All 16 source variants: **{source:.2f} MiB**. Per-level runtime texture totals: " +
             ', '.join(f"LOD{i} **{value:.2f} MiB**" for i,value in enumerate(levels)) + '.', '',
        f"The renderer lazily uploads a type when it first appears, sharing its meshes and textures among its cars. "
        f"All four levels of all types resident together use **{sum(levels):.2f} MiB** of texture storage. "
        "These are residency estimates, not per-frame texture bandwidth or FPS predictions. Fiat variants currently "
        "have separate allocations even for identical image content. Source JPEG/PNG file size is not GPU memory size.", '',
        "LOD0 keeps the selected geometry and caps images at 1024 px; LOD1/2/3 cap them at 512/256/128 px. "
        "The Octavia source's 4096² map alone costs 85.33 MiB with mips; its 1024² runtime map costs 5.33 MiB. "
        "For a roughly 1000-triangle car this is the clearest avoidable texture cost. The five 128² cars are already small.", '',
        "The current uploader generates mipmaps. Distant cars already sample smaller mip levels, so shrinking the "
        "source mainly saves resident memory, decoding and upload work; it does not imply a proportional FPS increase. "
        "Mipmap filtering can improve texture cache use and bandwidth, as described by the "
        "[Khronos performance guide](https://wikis.khronos.org/opengl/Performance). "
        "The renderer shades base-color maps, not imported PBR specular/normal effects, but currently uploads all retained "
        "texture objects. Astra/Fiat texture totals include maps that may not be sampled by the shader.", '',
        "Mesh reduction requests 65%, 35% and 15% of LOD0. Error limits and material/UV boundaries take precedence, "
        "so the measured counts can be higher; the very small cars sometimes retain their near geometry. "
        "Screen coverage, material draw calls, lighting and shadows still matter. Large fleets may benefit more from "
        "future instanced material batches than from further triangle reduction.", '',
        "See [all LoD comparisons](lod-comparisons/overview.png), [runtime policy and reproduction](README.md#vehicle-lods), "
        "and the machine-readable [inventory](lod-report.json).", '']
    atomic(root / 'LOD_REPORT.md', '\n'.join(text).encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets/vehicles')
    parser.add_argument('--helper', type=Path, default=ROOT / 'build/vehicle_mesh_simplify')
    args = parser.parse_args()
    root = args.assets
    preparation = json.loads((root / 'preparation.json').read_text())
    settings = json.loads((root / 'lod-settings.json').read_text())
    if settings['version'] != 1 or not args.helper.is_file():
        parser.error('Expected settings version 1 and a built vehicle_mesh_simplify helper')
    manifest = {'version': 1, 'models': {}}
    report = {'version': 1, 'variants': []}
    cards = []
    for row in preparation['variants']:
        source = root / row['output_glb']
        source_bytes = source.read_bytes()
        source_hash = hashlib.sha256(source_bytes).hexdigest()
        if source_hash != row['output_sha256']:
            raise ValueError('Stale prepared model: ' + row['slug'])
        doc, binary = read_glb(source_bytes)
        inventory = texture_inventory(doc, binary)
        doc, binary = chassis_model(doc, binary, settings['yaw_degrees'][row['slug']], settings['length_m'], settings['bottom_m'])
        generated = generate_levels(doc, binary, args.helper.resolve())
        entry = {'source': source.name, 'source_sha256': source_hash, 'levels': []}
        reference = reference_for(geometry(generated[0][0]))
        canvas = Image.new('RGB', (1280, 680), '#131923')
        draw = ImageDraw.Draw(canvas)
        draw.text((18, 12), row['name'] + ' | chassis-local LoDs', font=font(23), fill='white')
        for level, (data, stats) in enumerate(generated):
            digest = hashlib.sha256(data).hexdigest()
            relative = f'lod{level}/{row["slug"]}-{digest[:16]}.glb'
            atomic(root / 'models' / relative, data)
            entry['levels'].append({'path': relative, 'sha256': digest, **stats})
            x = level * 320
            draw.text((x + 12, 58), f'LOD{level}: {stats["triangles"]} tris', font=font(20), fill='#76deb5')
            draw.text((x + 12, 86), f'maps <= {stats["texture_limit"]} px', font=font(17), fill='#b7c9df')
            meshes = geometry(data)
            # Front and rear views expose accidentally reversed source frames.
            canvas.paste(render(meshes, reference, (1.3, .65, -1.6), size=(320, 260)), (x, 118))
            canvas.paste(render(meshes, reference, (-1.3, .65, 1.6), size=(320, 260)), (x, 380))
        draw.text((18, 650), 'CC BY 4.0 | ' + row['author'] + ' | CPU previews; front view above, rear below', font=font(15), fill='#97abc4')
        atomic(root / 'lod-comparisons' / (row['slug'] + '.png'), image_bytes(canvas))
        cards.append(canvas)
        manifest['models'][row['slug'].replace('-', '_')] = entry
        report['variants'].append({'slug': row['slug'], 'name': row['name'], 'source_textures': inventory,
                                   'source_sha256': source_hash, 'levels': entry['levels']})
        print(row['slug'] + ': ' + ' / '.join(str(s['triangles']) for s in entry['levels']), flush=True)
    # Content-addressed GLBs and final atomic manifest replacement leave an older
    # valid publication usable if generation or preview rendering is interrupted.
    atomic(root / 'models/lods.json', json_bytes(manifest))
    atomic(root / 'lod-report.json', json_bytes(report))
    write_report(report, root)
    sheet = Image.new('RGB', (1280, 80 + len(cards) * 340), '#101722')
    ImageDraw.Draw(sheet).text((20, 18), 'Vehicle LoDs | near to far: 0, 1, 2, 3', font=font(26), fill='white')
    for i, card in enumerate(cards):
        sheet.paste(card.resize((640, 340), Image.Resampling.LANCZOS), ((i % 2) * 640, 80 + (i // 2) * 340))
    sheet = sheet.crop((0, 0, 1280, 80 + ((len(cards) + 1) // 2) * 340))
    atomic(root / 'lod-comparisons/overview.png', image_bytes(sheet))


if __name__ == '__main__':
    main()
