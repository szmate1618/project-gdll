"""Render offline forest comparisons as satellite-backed HTML, PNG and metrics."""
from __future__ import annotations

import base64
import html
import io
import json
import os
from pathlib import Path
import tempfile
from itertools import combinations

from PIL import Image, ImageDraw, ImageFont

from .forest_comparison import (compare_forests, file_sha256, load_forest, load_tree_points, polygon_parts,
                                satellite_backdrop, svg_path)


ROOT = Path(__file__).resolve().parents[1]
COLORS = {"old": "#ffb84d", "new": "#53cfff", "shared": "#b4df80",
          "added": "#53cfff", "removed": "#ffb84d", "original": "#ffb84d",
          "osm": "#d7a8ff", "sentinel-astra": "#53cfff"}


def _font(size):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                 "/usr/share/fonts/TTF/DejaVuSans.ttf"):
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size=size)


def _paint(image, geometry, bounds, color):
    """Draw through a hole-aware mask so underlying imagery remains visible."""
    from PIL import ImageColor
    x0, y0, x1, y1 = bounds
    def coords(ring):
        return [((x-x0)/(x1-x0)*image.width, (y1-y)/(y1-y0)*image.height) for x, y in ring.coords]
    mask = Image.new("L", image.size)
    draw = ImageDraw.Draw(mask)
    for polygon in polygon_parts(geometry):
        draw.polygon(coords(polygon.exterior), fill=90)
        draw.line(coords(polygon.exterior), fill=245, width=2)
        for ring in polygon.interiors:
            draw.polygon(coords(ring), fill=0)
            draw.line(coords(ring), fill=245, width=2)
    image.paste(Image.new("RGB", image.size, ImageColor.getrgb(color)), (0, 0), mask)


def _overview(background, bounds, geometries, metrics, datasets=None):
    panel_width, margin, header = 900, 30, 180
    panel_height = round(background.height / background.width * panel_width)
    canvas = Image.new("RGB", (panel_width*3 + margin*4, panel_height + header + 130), "#111b25")
    draw = ImageDraw.Draw(canvas)
    draw.text((margin, 22), "Forest coverage / Original · OSM · Sentinel-Astra" if datasets else
              "Forest coverage / Original vs Sentinel-Astra", font=_font(34), fill="white")
    draw.text((margin, 70), "Satellite imagery only for Astra and the backdrop. Existing polygons retained unchanged.",
              font=_font(22), fill="#b8c8d7")
    titles = [f"Existing · {metrics['old_area_m2']/1e6:.3f} km²",
              f"Astra · {metrics['new_area_m2']/1e6:.3f} km²", "Coverage difference"]
    modes = ("old", "new", "difference")
    if datasets:
        modes = tuple(datasets)
        titles = [f"{d['label']} · {d['area_m2']/1e6:.3f} km²" for d in datasets.values()]
    for i, mode in enumerate(modes):
        left = margin + i*(panel_width+margin)
        panel = background.resize((panel_width, panel_height), Image.Resampling.LANCZOS)
        names = ("shared", "added", "removed") if mode == "difference" else (mode,)
        for name in names:
            _paint(panel, geometries[name], bounds, COLORS[name])
        canvas.paste(panel, (left, header))
        draw.text((left, 124), titles[i], font=_font(28), fill="white")
        draw.text((left+panel_width-35, header+12), "N ↑", font=_font(22), fill="white", stroke_width=2, stroke_fill="#111b25")
        scale_width = round(1000/(bounds[2]-bounds[0])*panel_width)
        bar_y = header + panel_height - 28
        draw.line((left+20, bar_y, left+20+scale_width, bar_y), fill="white", width=4)
        draw.text((left+20, bar_y-30), "1 km", font=_font(20), fill="white", stroke_width=1, stroke_fill="#111b25")
    y = header + panel_height + 28
    for i, name in enumerate(modes if datasets else ("shared", "added", "removed")):
        x = margin+i*(panel_width+margin)
        draw.rectangle((x, y+4, x+20, y+24), fill=COLORS[name])
        if datasets:
            label = f"{datasets[name]['patch_count']} forest patches"
            if name == "osm":
                label += f" · {datasets[name]['point_count']} mapped trees"
        else:
            label = {"shared": "In both", "added": "Astra only", "removed": "Existing only"}[name]
            label = f"{label}: {metrics[name+'_area_m2']/1e6:.3f} km²"
        draw.text((x+32, y), label, font=_font(25), fill="white")
    draw.text((margin, y+48), "Differences reflect tracing methods and source dates; they are not measured forest change.",
              font=_font(21), fill="#b8c8d7")
    return canvas


def create_comparison(old_path, new_path, imagery_path, output_dir, map_path=None,
                      osm_path=None, osm_trees_path=None):
    old_path, new_path, imagery_path, output_dir = map(Path, (old_path, new_path, imagery_path, output_dir))
    old, old_geometry = load_forest(old_path)
    new, new_geometry = load_forest(new_path)
    metrics, layers = compare_forests(old, old_geometry, new, new_geometry)
    if map_path is not None and file_sha256(map_path) != new["source"]["map_sha256"]:
        raise ValueError("Forest fingerprints do not match the published map")
    datasets = {"original": (old_path, old, old_geometry),
                "sentinel-astra": (new_path, new, new_geometry)}
    if osm_path is not None:
        osm_path = Path(osm_path)
        osm, osm_geometry = load_forest(osm_path)
        compare_forests(old, old_geometry, osm, osm_geometry)
        datasets = {"original": datasets["original"], "osm": (osm_path, osm, osm_geometry),
                    "sentinel-astra": datasets["sentinel-astra"]}
    if osm_trees_path is not None and osm_path is None:
        raise ValueError("OSM tree points require an OSM forest configuration")
    tree_points = load_tree_points(osm_trees_path, datasets["osm"][1]) if osm_trees_path else []
    inputs = [record[0] for record in datasets.values()]
    if osm_trees_path:
        inputs.append(Path(osm_trees_path))
    for path in inputs:
        if output_dir.resolve() == path.resolve() or output_dir.resolve() in path.resolve().parents:
            raise ValueError("Comparison destination must not contain either input configuration")
    bounds = old["coordinate_frame"]["bounds_m"]
    background = satellite_backdrop(imagery_path, old["coordinate_frame"])
    encoded = io.BytesIO()
    background.save(encoded, format="JPEG", quality=90)
    geometries = {"old": old_geometry, "new": new_geometry, **layers}
    labels = {"original": "Original", "osm": "OSM", "sentinel-astra": "Sentinel-Astra"}
    dataset_info, comparisons, layer_markup = {}, {}, []
    for identifier, (path, document, geometry) in datasets.items():
        dataset_info[identifier] = {"label": labels[identifier], "path": str(path),
            "sha256": file_sha256(path), "area_m2": geometry.area,
            "patch_count": len(document["patches"]),
            "point_count": len(tree_points) if identifier == "osm" else 0}
        geometries[identifier] = geometry
        layer_markup.append(f'<g class="layer" data-source="{identifier}" style="display:none">'
                            f'<path class="{identifier}" d="{svg_path(geometry, bounds)}"/></g>')
    for a, b in combinations(datasets, 2):
        pair_metrics, pair_layers = compare_forests(datasets[a][1], datasets[a][2], datasets[b][1], datasets[b][2])
        pair_id = f"{a}|{b}"
        comparisons[pair_id] = {"base": a, "target": b, **pair_metrics}
        layer_markup.append(f'<g class="layer" data-pair="{pair_id}" style="display:none">'
            f'<path class="shared" d="{svg_path(pair_layers["shared"], bounds)}"/>'
            f'<path class="{a}" d="{svg_path(pair_layers["removed"], bounds)}"/>'
            f'<path class="{b}" d="{svg_path(pair_layers["added"], bounds)}"/></g>')
    x0, y0, x1, y1 = bounds
    point_markup = ''.join(f'<circle cx="{(x-x0)/(x1-x0)*1000:.3f}" '
                           f'cy="{(y1-y)/(x1-x0)*1000:.3f}" r="1.7"/>' for x, y in tree_points)
    layer_markup.append(f'<g id="osm-points" class="tree-points" style="display:none">{point_markup}</g>')
    height = 1000*(bounds[3]-bounds[1])/(bounds[2]-bounds[0])
    template = (ROOT / "tools/templates/forest_comparison.html").read_text(encoding="utf-8")
    replacements = {
        "@@BACKGROUND@@": base64.b64encode(encoded.getvalue()).decode("ascii"),
        "@@HEIGHT@@": f"{height:.6f}",
        "@@DATA@@": json.dumps({"datasets": dataset_info, "comparisons": comparisons}),
        "@@LAYERS@@": "\n".join(layer_markup),
        "@@STATS@@": ''.join(f'<div class="stat"><span>{d["label"]}</span>'
                             f'<strong>{d["area_m2"]/1e6:.3f} km²</strong>'
                             f'<span>{d["patch_count"]} forest patches</span></div>'
                             for d in dataset_info.values()),
        "@@SCALE@@": str(1000*1000/(bounds[2]-bounds[0])),
        "@@IMAGERY@@": html.escape(imagery_path.name),
    }
    for key, value in replacements.items():
        template = template.replace(key, value)
    if "@@" in template:
        raise ValueError("Unresolved comparison template token")
    report = {"schema": "godollo.forest_comparison", "version": 1,
              "old": {"path": str(old_path), "sha256": file_sha256(old_path)},
              "new": {"path": str(new_path), "sha256": file_sha256(new_path)},
              "imagery": {"path": str(imagery_path), "sha256": file_sha256(imagery_path)},
              "map_sha256": new["source"]["map_sha256"], "metrics": metrics,
              "datasets": dataset_info, "comparisons": comparisons,
              "note": "Coverage disagreement, not measured temporal forest change."}
    if osm_trees_path:
        report["osm_trees"] = {"path": str(osm_trees_path), "sha256": file_sha256(osm_trees_path),
                               "count": len(tree_points)}
    output_dir.mkdir(parents=True, exist_ok=True)
    # Render every candidate successfully before replacing any existing artifact.
    with tempfile.TemporaryDirectory(prefix=".comparison-", dir=output_dir) as scratch:
        scratch = Path(scratch)
        (scratch / "index.html").write_text(template, encoding="utf-8")
        (scratch / "metrics.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
        _overview(background, bounds, geometries, metrics,
                  dataset_info if osm_path else None).save(scratch / "comparison.png")
        for name in ("index.html", "metrics.json", "comparison.png"):
            os.replace(scratch / name, output_dir / name)
    return report
