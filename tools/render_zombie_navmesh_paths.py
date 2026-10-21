#!/usr/bin/env python3
"""Render recorded routes over the actual map's software aerial raster.

No map generation, downloads, graphics context or AI image generation is used.
Requires Pillow; reads benchmark_zombie_navmesh's paths.json and aerial.ppm.
"""

import argparse
import colorsys
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def font(size, bold=False):
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    try:
        return ImageFont.truetype(name, size)
    except OSError:
        return ImageFont.load_default()


def render(source, destination):
    document = json.loads(source.read_text())
    raster = document["raster"]
    background = Image.open(source.parent / raster["file"]).convert("RGBA")
    minimum, maximum = raster["minimum"], raster["maximum"]
    scale, margin = raster["scale"], raster["margin"]

    def project(point):
        return ((point[0] - minimum[0]) * scale + margin,
                (point[1] - minimum[1]) * scale + margin)

    # Keep each footprint oriented in its own local frame. Trunks get a 1 px
    # marker at overview scale so that their actual subpixel boxes are visible.
    footprint = Image.new("RGBA", background.size)
    draw = ImageDraw.Draw(footprint)
    palette = [(180, 69, 54, 65), (35, 118, 53, 95), (180, 103, 29, 150)]
    for kind, x, z, ux, uz, hx, hz in document["obstacles"]:
        corners = [project((x + a * ux * hx - b * uz * hz,
                            z + a * uz * hx + b * ux * hz))
                   for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        if kind == 1 and max(hx, hz) * scale < 0.6:
            px, py = project((x, z))
            draw.ellipse((px - 0.6, py - 0.6, px + 0.6, py + 0.6), fill=palette[kind])
        else:
            draw.polygon(corners, fill=palette[kind])
    background = Image.alpha_composite(background, footprint)
    destination.mkdir(parents=True, exist_ok=True)
    thumbnails = []
    relative_goals = document.get("goal_layout") == "relative_offsets"
    random_pairs = document.get("goal_layout") == "random_pairs"
    weight = document.get("heuristic_weight", 1)
    mesh = document["navmesh"]
    polygon_count = mesh.get("polygons", mesh.get("triangles", 0))
    for batch in document["batches"]:
        route_layer = Image.new("RGBA", background.size)
        draw = ImageDraw.Draw(route_layer)
        successful = [route for route in batch["routes"] if route["points"]]
        for index, route in enumerate(successful):
            points = [project(point) for point in route["points"]]
            hue = (index * 0.618033988749895 + 0.54) % 1
            color = tuple(round(255 * c) for c in colorsys.hsv_to_rgb(hue, 0.95, 0.77))
            draw.line(points, fill=(*color, 170 if len(successful) > 10 else 255),
                      width=2 if len(successful) > 100 else 3)
            if len(successful) <= 10:
                for x, y in points[1:-1]:
                    draw.ellipse((x-2.5, y-2.5, x+2.5, y+2.5), fill=(255, 255, 255, 255),
                                 outline=(*color, 255), width=1)
        for route in batch["routes"]:
            x, y = project(route["start"])
            if route["points"]:
                draw.ellipse((x-2, y-2, x+2, y+2), fill=(0, 178, 134, 230))
            else:
                draw.line((x-4, y-4, x+4, y+4), fill=(219, 28, 42, 255), width=2)
                draw.line((x-4, y+4, x+4, y-4), fill=(219, 28, 42, 255), width=2)
        goals = list(dict.fromkeys(tuple(route["goal"]) for route in batch["routes"]))
        for endpoint in goals:
            goal = project(endpoint)
            radius = 3 if len(goals) > 100 else 6 if len(goals) > 1 else 11
            draw.regular_polygon((goal[0], goal[1], radius), 3, rotation=0,
                                 fill=(235, 41, 104, 255),
                                 outline=(255, 255, 255, 255) if len(goals) <= 100 else None,
                                 width=1 if len(goals) > 1 else 2)
        overlay = Image.alpha_composite(background, route_layer).convert("RGB")
        map_width, map_height = overlay.size
        # Full map and three square close-ups preserve north-up geometry while
        # making thin right-edge routes readable at full-town scale.
        panel_width = 560
        header, footer = (214 if relative_goals or random_pairs else 187), 86
        canvas = Image.new("RGB", (map_width + panel_width + 50, map_height + header + footer), "#f6f7f8")
        canvas.paste(overlay, (20, header))
        annotation = ImageDraw.Draw(canvas)
        count, failures = batch["zombies"], batch["failed"]
        subject = "zombie" if count == 1 else "zombies"
        algorithm = ("Navmesh + pure greedy (h) + funnel" if document.get("greedy") else
                     f"Navmesh + weighted A* (g + {weight:g}h) + funnel")
        annotation.text((25, 18), f"{algorithm} routes · {count:,} {subject}", font=font(30, True), fill="#152936")
        annotation.text((25, 65),
                        f"{len(successful):,} found  /  {failures:,} failed  ·  "
                        f"{batch['median_ms']:.3f} ms median total  ·  "
                        f"{batch['median_ms'] * 1000 / count:.2f} µs / zombie",
                        font=font(21), fill="#334a57")
        annotation.text((25, 105),
                        f"Actual map overhead · X east / Z south · north is up · "
                        f"{document['repeats']} measured repeats",
                        font=font(18), fill="#536773")
        if relative_goals:
            annotation.text((25, 134),
                            "Independent searches · destination offsets match starting offsets · "
                            + ("closed polygons reopened" if document.get("reopen_closed")
                               else "each polygon expanded at most once"),
                            font=font(18), fill="#536773")
        if random_pairs:
            annotation.text((25, 134),
                            f"Independent searches · random walkable starts and goals · seed {document['seed']}",
                            font=font(18), fill="#536773")
        connectivity = (f"{batch['connectivity_rejected_requests']:,} disconnected / "
                        if document.get("connectivity_precheck", {}).get("enabled") else "")
        annotation.text((25, 166 if relative_goals or random_pairs else 134),
                        f"{polygon_count:,} polygons · "
                        f"{document['search_node_limit']:,} stored search-polygon limit · "
                        f"{batch['status_counts'].get('blocked_endpoint', 0):,} blocked endpoints / "
                        + connectivity + f"{batch['status_counts'].get('search_limit', 0):,} search limits",
                        font=font(18), fill="#536773")
        annotation.text((map_width - 80, header + 30), "N ↑", font=font(24, True), fill="#102e46")

        # Pick an interesting middle close-up at a bend, rather than an arbitrary
        # empty map center. The other two show start and destination regions.
        reference = max(successful, key=lambda route: len(route["points"]), default=batch["routes"][0])
        points = reference["points"] or [reference["start"], reference["goal"]]
        centers = [batch["routes"][0]["goal"], points[len(points)//2], batch["routes"][0]["start"]]
        labels = ["Destination · upper right", "Route detail · middle bend", "Starts · lower right"]
        if relative_goals:
            for panel, field in ((0, "goal"), (2, "start")):
                endpoints = [route[field] for route in batch["routes"]]
                centers[panel] = [(min(point[axis] for point in endpoints) +
                                   max(point[axis] for point in endpoints)) / 2
                                  for axis in range(2)]
            labels[0] = "Destinations · preserved offsets"
        if random_pairs:
            centers[0], centers[2] = reference["goal"], reference["start"]
            labels = ["Random route · destination", "Random route · middle bend",
                      "Random route · start"]
        detail_size = min(panel_width - 20, (map_height - 140) // 3)
        for panel_index, (center, label) in enumerate(zip(centers, labels)):
            x, y = project(center)
            # 700 m square in world coordinates, resampled consistently in X/Z.
            half = 350 * scale
            crop_span = min(map_width, map_height, max(1, round(2*half)))
            crop_x = max(0, min(round(x-half), map_width-crop_span))
            crop_y = max(0, min(round(y-half), map_height-crop_span))
            box = (crop_x, crop_y, crop_x+crop_span, crop_y+crop_span)
            crop = Image.new("RGB", (box[2]-box[0], box[3]-box[1]), "#edf0f2")
            crop.paste(overlay, (-box[0], -box[1]))
            crop = crop.resize((detail_size, detail_size), Image.Resampling.LANCZOS)
            left = map_width + 35
            top = header + 30 + panel_index * (detail_size + 46)
            annotation.text((left, top-25), label, font=font(19, True), fill="#263f4d")
            canvas.paste(crop, (left, top))
            annotation.rectangle((left, top, left+detail_size, top+detail_size), outline="#b6c3c9", width=1)
        legend_y = header + map_height + 15
        goal_legend = "Goals: pink triangles" if len(goals) > 1 else "Goal: pink triangle"
        annotation.text((25, legend_y),
                        f"Routes: colored lines   Starts: green dots   {goal_legend}   Failures: red ×",
                        font=font(18), fill="#263f4d")
        annotation.text((25, legend_y+29),
                        ("Obstacle boxes: buildings red · trunks green · fences orange. Insets show 700 m squares."
                         if any(obstacle[0] == 1 for obstacle in document["obstacles"]) else
                         "Trees ignored for navigation; aerial vegetation is context. Buildings red · fences orange."), font=font(16), fill="#536773")
        path = destination / f"zombie_paths_{count:04d}.png"
        canvas.save(path)
        print(path)
        thumb = canvas.copy()
        thumb.thumbnail((1150, 1100), Image.Resampling.LANCZOS)
        thumbnails.append(thumb)
    if thumbnails:
        if len(thumbnails) == 1:
            contact = canvas
        else:
            tile_w = max(image.width for image in thumbnails)
            tile_h = max(image.height for image in thumbnails)
            columns = min(2, len(thumbnails))
            rows = (len(thumbnails) + columns - 1) // columns
            contact = Image.new("RGB", (tile_w * columns, tile_h * rows), "#e4e9ed")
            for index, thumbnail in enumerate(thumbnails):
                contact.paste(thumbnail, ((index % columns) * tile_w, (index // columns) * tile_h))
        contact.save(destination / "zombie_paths_all.png")
        print(destination / "zombie_paths_all.png")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, help="paths.json produced by benchmark_zombie_navmesh")
    parser.add_argument("--output", type=Path, help="PNG directory; default is beside paths.json")
    args = parser.parse_args()
    render(args.paths, args.output or args.paths.parent)
