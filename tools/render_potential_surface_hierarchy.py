#!/usr/bin/env python3
"""Build raster contact sheets from the four completed guidance benchmark images."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from compare_potential_surface_hierarchy import VARIANTS

LABELS = {'baseline': 'Tangent baseline', 'hierarchy': 'Hierarchical field',
          'pockets': 'Pocket targets', 'combined': 'Hierarchy + pocket targets'}


def font(size, bold=False):
    try:
        return ImageFont.truetype('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf', size)
    except OSError:
        return ImageFont.load_default()


def render(directory, cohort):
    directory = Path(directory).resolve()
    comparison = json.loads((directory/'comparison.json').read_text())
    scenario = comparison['scenarios'][cohort]
    images = []
    for variant, _ in VARIANTS:
        with Image.open(directory/f'{cohort}-{variant}/potential-surface-paths.png') as source:
            image = source.convert('RGB')
        image.thumbnail((1200, 1200), Image.Resampling.LANCZOS)
        images.append((variant, image))
    cell_width = 1200
    cell_height = max(image.height for _, image in images)+86
    header = 100
    canvas = Image.new('RGB', (cell_width*2, header+cell_height*2), '#f6f7f8')
    draw = ImageDraw.Draw(canvas)
    draw.text((20,12), f'Potential Surface Steering guidance · {cohort} · seed {scenario["seed"]}',
              font=font(28, True), fill='#173440')
    draw.text((20,54), 'Identical 1,000 trips · physical collision checks retained · CPU figures are single-pass observations',
              font=font(20), fill='#536773')
    for index, (variant, image) in enumerate(images):
        x, y = (index%2)*cell_width, header+(index//2)*cell_height
        entry = scenario['variants'][variant]
        status = entry['statuses']
        draw.text((x+15,y+6), LABELS[variant], font=font(25, True), fill='#173440')
        text = (f'{status["arrived"]} arrived · {status["stalled"]} stalled · {status["timeout"]} timeout'
                f' · CPU/tick {entry["matched_tick_cpu_ms"]["mean"]:.3f} ms')
        draw.text((x+15,y+43), text, font=font(19), fill='#536773')
        canvas.paste(image, (x,y+82))
    output = directory/f'{cohort}-guidance-comparison.png'
    canvas.save(output)
    print(output, flush=True)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--cohort', choices=('training', 'second-seed', 'both'), default='both')
    args = parser.parse_args()
    for name in ('training', 'second-seed') if args.cohort == 'both' else (args.cohort,):
        render(args.directory, name)
