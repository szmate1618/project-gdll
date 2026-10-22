#!/usr/bin/env python3
"""Draw sparse analytical guidance geometry; these images are diagnostic only."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

from render_potential_surface import draw_geometry, font
from run_potential_surface_experiment import sha


def render(scene_path, guidance_path, output):
    scene = json.loads(scene_path.read_text())
    guidance = json.loads(guidance_path.read_text())
    if guidance['source_sha256'] != sha(scene_path):
        raise ValueError('Guidance and aerial scene fingerprints differ')
    raster = scene['raster']
    background = Image.open(scene_path.parent/raster['file']).convert('RGBA')
    layer = Image.new('RGBA', background.size)
    draw = ImageDraw.Draw(layer)

    def project(point):
        return tuple((point[i]-raster['minimum'][i])*raster['scale']+raster['margin'] for i in range(2))

    for bump in guidance['bumps']:
        c, r = bump['center'], bump['radii']
        box = (*project((c[0]-r[0], c[1]-r[1])), *project((c[0]+r[0], c[1]+r[1])))
        draw.ellipse(box, outline=(40, 110, 225, 95) if bump['level'] == 0 else (130, 50, 180, 120))
    for pocket in guidance['pockets']:
        draw.polygon([project(p) for p in pocket['outer']], fill=(255, 185, 10, 145))
        for ring in pocket['holes']:
            draw.polygon([project(p) for p in ring], fill=(0, 0, 0, 0))
        draw.line([project(p) for p in pocket['mouth']], fill=(0, 180, 140, 255), width=3)
    image = Image.alpha_composite(background, layer).convert('RGB')
    caption = Image.new('RGB', (image.width, image.height+85), '#f5f6f8')
    caption.paste(image, (0, 85))
    draw = ImageDraw.Draw(caption)
    draw.text((20, 12), 'Analytical structural guidance: stored geometry, no sampled surface', font=font(25, True), fill='#182331')
    draw.text((20, 47), f"{len(guidance['bumps'])} hierarchy ellipses (blue/purple); {len(guidance['pockets'])} pockets (gold), mouths (green)", font=font(20), fill='#273547')
    output.mkdir(parents=True, exist_ok=True)
    caption.save(output/'guidance-geometry.png')

    selected = sorted(enumerate(guidance['pockets']), key=lambda item: (-item[1]['depth'], item[0]))[:6]
    if not selected:
        return
    size = 520
    sheet = Image.new('RGB', (size*3, (size+90)*2), '#f5f6f8')
    for number, (idx, pocket) in enumerate(selected):
        points = pocket['outer']
        low = [min(p[i] for p in points) for i in range(2)]
        high = [max(p[i] for p in points) for i in range(2)]
        center = [(a+b)*.5 for a, b in zip(low, high)]
        half = max(max(b-a for a, b in zip(low, high))*.65, 12)
        origin = [p-half for p in center]
        scale = size/(half*2)

        def local(point):
            return tuple((point[i]-origin[i])*scale for i in range(2))

        a, b = project(origin), project([p+half for p in center])
        panel = background.crop((*a, *b)).resize((size, size)).convert('RGBA')
        overlay = Image.new('RGBA', panel.size)
        draw_geometry(overlay, scene['obstacles'], local, scale, True, center, half)
        draw = ImageDraw.Draw(overlay)
        draw.polygon([local(p) for p in points], fill=(255, 185, 10, 100))
        for ring in pocket['holes']:
            draw.polygon([local(p) for p in ring], fill=(0, 0, 0, 0))
            draw.line([local(p) for p in ring], fill=(180, 40, 40, 230), width=3)
        draw.line([local(p) for p in pocket['mouth']], fill=(0, 160, 110, 255), width=4)
        middle = [sum(p[i] for p in pocket['mouth'])*.5 for i in range(2)]
        target = [middle[i]+2*pocket['outward'][i] for i in range(2)]
        x, y = local(target)
        draw.ellipse((x-4, y-4, x+4, y+4), fill=(20, 70, 255, 255))
        panel = Image.alpha_composite(panel, overlay).convert('RGB')
        left, top = number % 3*size, number//3*(size+90)
        sheet.paste(panel, (left, top+90))
        draw = ImageDraw.Draw(sheet)
        draw.text((left+12, top+10), f'Pocket {idx}: depth {pocket["depth"]:.1f} m / mouth {pocket["width"]:.1f} m', font=font(20, True), fill='#182331')
        draw.text((left+12, top+43), 'Gold: free pocket; green: mouth; blue: exit target', font=font(17), fill='#273547')
    sheet.save(output/'pocket-examples.png')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--guidance', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    render(args.scene, args.guidance, args.output)
