"""Download public geodata and export a single Gödöllő GLB scene."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import logging
import os
from pathlib import Path
import tempfile
import time

# All paths are anchored to this project, even when invoked from another cwd.
ROOT = Path(__file__).resolve().parents[1]
TEMP = ROOT / "data" / "tmp"
TEMP.mkdir(parents=True, exist_ok=True)
os.environ["TMPDIR"] = str(TEMP)
os.environ["CPL_TMPDIR"] = str(TEMP)
os.environ["PROJ_NETWORK"] = "OFF"
tempfile.tempdir = str(TEMP)

import numpy as np
import trimesh

from .buildings import add_buildings
from .config import coordinate_frame, load_config
from .data import download_sources
from .osm import parse_osm
from .roads import add_roads
from .terrain import build_terrain
from .verify import inspect_glb

LOG = logging.getLogger("godollo")


def run(config: dict) -> dict:
    started = time.monotonic()
    for directory in ("data/raw/osm", "data/raw/copernicus_dem", "data/raw/sentinel", "data/processed", "output"):
        (ROOT / directory).mkdir(parents=True, exist_ok=True)
    frame = coordinate_frame(config)
    LOG.info("Gödöllő bbox %s; projected coordinates %s", config["bbox"], config["crs"])
    sources = download_sources(config, ROOT)
    terrain, terrain_mesh, terrain_stats = build_terrain(config, frame, sources, ROOT)
    scene = trimesh.Scene(base_frame="local_meters")
    scene.add_geometry(terrain_mesh, geom_name="terrain", node_name="terrain")
    LOG.info("Parsing and clipping OpenStreetMap footprints and roads")
    with sources["osm"].open(encoding="utf-8") as stream:
        buildings, roads = parse_osm(json.load(stream), frame["transformer"], frame["origin"], frame["clip"])
    LOG.info("Generating %d building features and %d road features", len(buildings), len(roads))
    building_stats = add_buildings(scene, buildings, terrain, config)
    geometry_config = {**config, "_clip_polygon": frame["clip"]}
    road_stats = add_roads(scene, roads, terrain, geometry_config)
    if not building_stats.get("buildings") or not road_stats.get("road_segments"):
        raise ValueError("Selected bbox yielded no buildings or no roads; inspect OSM coverage or choose another bbox.")
    # Right-handed, glTF-standard Y up: X east, Y elevation, Z south.
    rotation = np.array([[1., 0., 0., 0.], [0., 0., 1., 0.], [0., -1., 0., 0.], [0., 0., 0., 1.]])
    scene.graph.update(frame_from="world", frame_to="local_meters", matrix=rotation)
    scene.graph.base_frame = "world"
    provenance = sources["provenance"]
    attribution = (ROOT / "ATTRIBUTION.md").read_text(encoding="utf-8")
    sentinel_meta = provenance.get("sentinel", {})
    acquisition = sentinel_meta.get("datetime") or sentinel_meta.get("acquisition_datetime")
    if acquisition:
        attribution = attribution.replace("data 2025 (default\n  acquisition range)", f"data {str(acquisition)[:4]} (selected acquisition)")
    metadata = {
        "name": "Gödöllő, Hungary", "bbox_wgs84": config["bbox"], "crs": config["crs"],
        "origin_wgs84": list(frame["center"]), "origin_projected_m": list(frame["origin"]),
        "origin_elevation_m": terrain.datum, "vertical_datum": "Copernicus EGM2008 orthometric height",
        "units": "meters", "gltf_axes": {"X": "projected east", "Y": "up", "Z": "projected south"},
        "terrain": terrain_stats, "buildings": building_stats, "roads": road_stats,
        "sources": provenance, "attribution": attribution,
    }
    scene.metadata.update(metadata)
    out = ROOT / "output" / "godollo.glb"
    pending = out.with_suffix(".pending.glb")
    LOG.info("Exporting self-contained GLB with embedded Sentinel texture")
    pending.write_bytes(scene.export(file_type="glb"))
    try:
        verification = inspect_glb(pending)
    except Exception:
        pending.unlink(missing_ok=True)
        raise
    pending.replace(out)
    report = {**metadata, "created_utc": datetime.now(timezone.utc).isoformat(),
              "output": "output/godollo.glb", "elapsed_seconds": round(time.monotonic() - started, 2),
              "verification": verification}
    (ROOT / "output" / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (ROOT / "output" / "ATTRIBUTION.md").write_text(attribution, encoding="utf-8")
    LOG.info("Saved %s (%.2f MiB)", out, verification["glb_bytes"] / 1024 ** 2)
    print(json.dumps({"output": str(out), "buildings": building_stats["buildings"],
                      "road_segments": road_stats["road_segments"],
                      "terrain_dimensions_m": terrain_stats["dimensions_m"],
                      "glb_bytes": verification["glb_bytes"], "bbox_wgs84": config["bbox"]}, indent=2))
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.json", help="JSON configuration (default: project config.json)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--offline", action="store_true", help="Use cached inputs only, with no network requests")
    mode.add_argument("--refresh", action="store_true", help="Refresh source searches and downloads")
    parser.add_argument("--verbose", action="store_true", help="Include debug messages and error traceback")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s", datefmt="%H:%M:%S")
    for noisy in ("rasterio", "PIL", "urllib3", "trimesh"):
        logging.getLogger(noisy).setLevel(logging.WARNING)
    try:
        config = load_config(args.config)
        if args.offline or args.refresh:
            config.update(offline=args.offline, refresh=args.refresh)
        run(config)
    except Exception as error:
        LOG.error("Generation failed: %s", error, exc_info=args.verbose)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
