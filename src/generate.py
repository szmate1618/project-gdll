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
from .data import download_osm, download_sources
from .fences import add_fences
from .fence_scene_update import update_fences
from .map_publication import publish_files
from .navmesh_preparation import prepare_navmesh
from .osm import parse_fences, parse_osm, parse_railways
from .railways import add_railways
from .railway_texture import configure_track_sampler
from .roads import add_roads
from .terrain import build_terrain
from .tree_preparation import prepare_trees
from .verify import inspect_glb

LOG = logging.getLogger("godollo")


def _prepare_default_trees(config, map_path, image_path=None):
    LOG.info("Preparing tuned2 individual trees with OSM forest exclusions")
    return prepare_trees(map_path=map_path, config_path=Path(config.get("_config_path", ROOT / "config.json")),
                         root=ROOT, image_path=image_path,
                         offline=config.get("offline", False), refresh=config.get("refresh", False))


def run(config: dict) -> dict:
    started = time.monotonic()
    for directory in ("data/raw/osm", "data/raw/copernicus_dem", "data/raw/sentinel", "data/processed", "output"):
        (ROOT / directory).mkdir(parents=True, exist_ok=True)
    frame = coordinate_frame(config)
    LOG.info("Gödöllő bbox %s; projected coordinates %s", config["bbox"], config["crs"])
    if config.get("trees_only"):
        result = _prepare_default_trees(config, ROOT / "output/godollo.glb")
        print(json.dumps(result, indent=2))
        return result
    if config.get("fences_only") and not (ROOT / "output/godollo.glb").is_file():
        raise ValueError("--fences-only requires an existing output/godollo.glb; run the full builder first.")
    if config.get("fences_only"):
        osm_path, provenance = download_osm(config, ROOT)
        LOG.info("Updating fences using the published town and terrain")
        report = update_fences(ROOT / "output/godollo.glb", config, osm_path, provenance)
        LOG.info("Saved verified fence update to %s", ROOT / "output/godollo.glb")
        _print_summary(report, ROOT / "output/godollo.glb")
        return report
    sources = download_sources(config, ROOT)
    terrain, terrain_mesh, terrain_stats = build_terrain(config, frame, sources, ROOT)
    scene = trimesh.Scene(base_frame="local_meters")
    scene.add_geometry(terrain_mesh, geom_name="terrain", node_name="terrain")
    LOG.info("Parsing OpenStreetMap footprints, roads, railways and fences")
    with sources["osm"].open(encoding="utf-8") as stream:
        osm = json.load(stream)
    buildings, roads = parse_osm(osm, frame["transformer"], frame["origin"], frame["clip"])
    railways = parse_railways(osm, frame["transformer"], frame["origin"], frame["clip"])
    fences = parse_fences(osm, frame["transformer"], frame["origin"], frame["clip"])
    LOG.info("Generating %d buildings, %d roads, %d railways and %d fences",
             len(buildings), len(roads), len(railways), len(fences))
    building_stats = add_buildings(scene, buildings, terrain, config, roads=roads)
    geometry_config = {**config, "_clip_polygon": frame["clip"]}
    road_stats = add_roads(scene, roads, terrain, geometry_config)
    railway_stats = add_railways(scene, railways, terrain, geometry_config)
    fence_stats = add_fences(scene, fences, terrain, geometry_config)
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
        "railways": railway_stats, "fences": fence_stats,
        "sources": provenance, "attribution": attribution,
    }
    scene.metadata.update(metadata)
    out = ROOT / "output" / "godollo.glb"
    LOG.info("Exporting self-contained GLB with embedded Sentinel texture")
    with tempfile.TemporaryDirectory(prefix=".map-", dir=out.parent) as scratch:
        staging = Path(scratch)
        pending = staging / out.name
        pending.write_bytes(scene.export(file_type="glb", tree_postprocessor=configure_track_sampler))
        verification = inspect_glb(pending)
        LOG.info("Baking tree-free zombie navigation from the verified map")
        navigation = prepare_navmesh(pending, staging / "godollo.navmesh")
        report = {**metadata, "created_utc": datetime.now(timezone.utc).isoformat(),
                  "output": "output/godollo.glb", "elapsed_seconds": round(time.monotonic() - started, 2),
                  "verification": verification, "navigation": navigation}
        (staging / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        (staging / "ATTRIBUTION.md").write_text(attribution, encoding="utf-8")
        publish_files({out.parent / path.name: path for path in staging.iterdir()})
    if not config.get("no_tree_preparation"):
        report["trees"] = _prepare_default_trees(config, out, image_path=sources.get("sentinel"))
        (ROOT / "output" / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    LOG.info("Saved %s (%.2f MiB)", out, verification["glb_bytes"] / 1024 ** 2)
    _print_summary(report, out)
    return report


def _print_summary(report: dict, out: Path) -> None:
    print(json.dumps({"output": str(out), "buildings": report["buildings"]["buildings"],
                      "navmesh": str(out.with_suffix(".navmesh")),
                      **({"tree_instances": report["trees"]["instances"]} if "trees" in report else {}),
                      "road_segments": report["roads"]["road_segments"],
                      "railway_segments": report["railways"]["railway_segments"],
                      "fence_ways": report["fences"]["fence_ways"],
                      "terrain_dimensions_m": report["terrain"]["dimensions_m"],
                      "glb_bytes": report["verification"]["glb_bytes"], "bbox_wgs84": report["bbox_wgs84"]}, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.json", help="JSON configuration (default: project config.json)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--offline", action="store_true", help="Use cached inputs only, with no network requests")
    mode.add_argument("--refresh", action="store_true", help="Refresh source searches and downloads")
    parser.add_argument("--verbose", action="store_true", help="Include debug messages and error traceback")
    layers = parser.add_mutually_exclusive_group()
    layers.add_argument("--fences-only", action="store_true", help="Update fences in the published map, reusing town geometry and terrain")
    layers.add_argument("--trees-only", action="store_true", help="Regenerate default tuned2 trees with OSM exclusions, reusing the town")
    parser.add_argument("--no-tree-preparation", action="store_true", help="Build town geometry without preparing tuned2 trees")
    args = parser.parse_args()
    if args.trees_only and args.no_tree_preparation:
        parser.error("Use --trees-only without --no-tree-preparation")
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s", datefmt="%H:%M:%S")
    for noisy in ("rasterio", "PIL", "urllib3", "trimesh"):
        logging.getLogger(noisy).setLevel(logging.WARNING)
    try:
        config = load_config(args.config)
        config["fences_only"] = args.fences_only
        config.update(trees_only=args.trees_only, no_tree_preparation=args.no_tree_preparation,
                      _config_path=str(args.config.resolve()))
        if args.offline or args.refresh:
            config.update(offline=args.offline, refresh=args.refresh)
        run(config)
    except Exception as error:
        LOG.error("Generation failed: %s", error, exc_info=args.verbose)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
