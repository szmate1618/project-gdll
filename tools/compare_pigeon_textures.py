#!/usr/bin/env python3
"""Compose matched pigeon renders into labeled texture-resolution comparisons."""
from pathlib import Path
import argparse

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
PREVIEWS = ROOT / 'assets/pigeon/previews'
BACKGROUND = '#20252b'
FOREGROUND = '#e8e8e5'
VIEWS = [('pigeon_palette', 'Standing'), ('pigeon_side', 'Side'), ('pigeon_flight', 'Flight')]


def font(size):
    return ImageFont.truetype('DejaVuSans.ttf', size)


def comparison(name, label, variants):
    canvas = Image.new('RGB', (1648, 696), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    for column, (suffix, title) in enumerate(variants):
        left = 16 + column * 816
        draw.text((left + 20, 16), title, font=font(32), fill=FOREGROUND)
        source = Image.open(PREVIEWS / f'{name}{suffix}.png').convert('RGB')
        if source.size != (1200, 1000):
            raise ValueError('Expected matching 1200×1000 source renders')
        # Identical crop and display scaling for both texture resolutions.
        panel = source.crop((100, 150, 1100, 850)).resize((800, 560), Image.Resampling.LANCZOS)
        canvas.paste(panel, (left, 66))
    draw.text((36, 642), f'{label} · 710 triangles · nearest texture filtering', font=font(24), fill=FOREGROUND)
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    tint = parser.add_mutually_exclusive_group()
    tint.add_argument('--warm-white', action='store_true', help='Compare the original and warm off-white 16×16 atlases')
    tint.add_argument('--yellow-beige', action='store_true', help='Compare the off-white and darker yellow-beige 16×16 atlases')
    args = parser.parse_args()
    if args.yellow_beige:
        variants = [('_16_warm', 'Off-white · #D1CCC9'), ('_16_yellow', 'Yellow-beige · #A5A198')]
        suffix = '_yellow_comparison'
        sheet_name = 'yellow_beige_comparison.png'
    elif args.warm_white:
        variants = [('_16', 'Before · 16 × 16'), ('_16_warm', 'Warm off-white · 16 × 16')]
        suffix = '_warm_comparison'
        sheet_name = 'warm_white_comparison.png'
    else:
        variants = [('', '512 × 512'), ('_16', '16 × 16')]
        suffix = '_comparison'
        sheet_name = 'texture_comparison.png'
    panels = []
    for name, label in VIEWS:
        panel = comparison(name, label, variants)
        panel.save(PREVIEWS / f'{name}{suffix}.png')
        panels.append(panel)
    sheet = Image.new('RGB', (1648, 696 * len(panels)), BACKGROUND)
    for row, panel in enumerate(panels):
        sheet.paste(panel, (0, row * 696))
    output = PREVIEWS / sheet_name
    sheet.save(output)
    print(f'Saved three matched comparisons and {output}')


if __name__ == '__main__':
    main()
