"""Render native Sentinel pixels, exclusion masks and tree candidates for review."""
from __future__ import annotations

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import binary_erosion


def render_review(rgb, excluded, training, matches, candidates, model, forest_source):
    width, height = 1050, round(rgb.shape[0] / rgb.shape[1] * 1050)
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    try:
        title_font, font = ImageFont.truetype(font_path, 30), ImageFont.truetype(font_path, 22)
    except OSError:
        title_font, font = ImageFont.load_default(size=30), ImageFont.load_default(size=22)
    background = Image.fromarray(rgb)
    canvas = Image.new("RGB", (width*3+100, height+235), "#15212b")
    draw = ImageDraw.Draw(canvas)
    hybrid_roles = {"hybrid": ("OSM", "Sentinel-Astra"), "hybrid-2": ("Sentinel-Astra", "OSM")}
    roles = hybrid_roles.get(forest_source)
    heading = (f"{forest_source.title()} satellite trees: {roles[0]} colour training / {roles[1]} forest exclusion"
               if roles else f"Satellite tree candidates outside {forest_source} forests")
    if forest_source == "sentinel-astra-tuned":
        heading = "Astra tuned: preserved forest-colour candidates + mixed-pixel recall proposals"
    elif forest_source == "sentinel-astra-tuned2":
        heading = "Astra tuned2: mixed-pixel proposals filtered by learned negative colour/context examples"
    draw.text((25, 18), heading, font=title_font, fill="white")
    mean = ', '.join(f'{v:.1f}' for v in model['mean_rgb'])
    draw.text((25, 64), f"Forest reference mean RGB: {mean} · {model['training_pixels']:,} training pixels · native pixel-centre positions",
              font=font, fill="#bed0dd")
    labels = ["Satellite image", f"{roles[1]} exclusion + {roles[0]} training" if roles else
              "Forest mask + training pixels", f"{len(candidates):,} non-forest tree candidates"]
    for panel_index in range(3):
        image = background.copy()
        if panel_index:
            overlay = np.asarray(image).astype(float).copy()
            overlay[excluded] = overlay[excluded]*.65 + np.array([180, 110, 240])*.35
            border = excluded & ~binary_erosion(excluded)
            overlay[border] = [210, 155, 255]
            if panel_index == 1:
                overlay[training] = overlay[training]*.55 + np.array([70, 220, 190])*.45
            image = Image.fromarray(np.uint8(np.clip(overlay, 0, 255)))
        image = image.resize((width, height), Image.Resampling.NEAREST)
        if panel_index == 2:
            markers = ImageDraw.Draw(image)
            for item in candidates:
                x = (item['column']+.5)/rgb.shape[1]*width
                y = (item['row']+.5)/rgb.shape[0]*height
                markers.ellipse((x-2.3, y-2.3, x+2.3, y+2.3), fill="#ffdf55", outline="#182029")
        left = 25+panel_index*(width+25)
        draw.text((left, 105), labels[panel_index], font=font, fill="white")
        canvas.paste(image, (left, 145))
    draw.text((25, height+166), "Purple: excluded forest · Teal: forest training · Yellow: candidate tree pixels · Pixel colours alone can also match lawns and shrubs.",
              font=font, fill="#bed0dd")
    draw.text((25, height+199), f"{int(matches.sum()):,} matching non-forest pixels before spacing · Tree positions and display dimensions are estimates, not measured crowns.",
              font=font, fill="#bed0dd")
    return canvas
