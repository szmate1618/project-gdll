# Tuned2 Sentinel tree preparation

The default workflow uses OSM exclusions and publishes the viewer's default
individual-tree layer:

```bash
.venv/bin/python tools/prepare_trees.py
# Equivalent, without rebuilding the town:
.venv/bin/python -m src.generate --trees-only --offline
```

The default exclusion file is `output/osm/forest_patches.json`. Missing or stale
OSM vegetation is prepared against the current map from cached inputs. The
standalone preparation command stays offline unless `--online` or `--refresh`
is supplied. Full town generation (`python -m src.generate`) runs the same
workflow after verifying its new GLB and respects `--offline`/`--refresh`.
`--no-tree-preparation` explicitly opts out; `--fences-only` retains its separate
fence workflow. A tree preparation failure makes full generation fail rather
than report success with an old layer; the verified town may already have been
published, so correct the inputs and retry with `--trees-only`.

Detector artifacts stay under `output/satellite_trees/tuned2-osm/`. Placements
are then published atomically to `output/trees.instances.json`, with relative
map and asset paths adjusted for that destination. The first previous default
is preserved verbatim as `output/trees.original.instances.json`. Later changed
defaults receive `trees.previous.<sha256>.instances.json` backups beside the
map; repeated publication of identical placements needs no new backup.

Run the viewer normally to use the new default. Existing runtime forest choices
are independent of the individual-tree detection exclusion mask. To display
the same OSM polygons, pass `--forests output/osm/forest_patches.json` without
`--vegetation osm`, which selects OSM individual trees too. To inspect the
preserved original placements, pass `--trees output/trees.original.instances.json`.
The original CSV preparation script remains `tools/prepare_tree_instances.py`.

## Separate detector output

Generate tree proposals using the accepted Astra tuned2 colour/context model,
with forest exclusion zones supplied explicitly:

```bash
.venv/bin/python tools/detect_tuned2_trees.py \
  --forest-exclusions forest_patches_astra.json
```

The required `--forest-exclusions` JSON controls where detection is forbidden.
It may be the original, OSM, Astra or another forest configuration, provided its
map fingerprint, CRS, projected origin and bounds match the published map.
Every native pixel touched by a polygon is excluded; holes remain searchable.

For example, exclude OSM forests and keep the result separate:

```bash
.venv/bin/python tools/detect_tuned2_trees.py \
  --forest-exclusions output/osm/forest_patches.json \
  --output-dir output/satellite_trees/tuned2-osm
```

Exclusions and colour calibration are separate inputs. Colour training still
uses `forest_patches_astra.json` and the saved profiles under
`assets/satellite_trees/`. Selecting OSM exclusions does not retrain the model
from OSM colours. `--colour-reference-forests` selects an explicitly matching
calibration reference when its location differs. Calibration fingerprints are
validated against that reference, the map, Sentinel image and recall profile;
the selected exclusion file is independently validated and fingerprinted.

The saved profiles are specific to the recorded native Sentinel image and
geographic frame. The default preparation workflow can reuse them after town
geometry changes: it still validates the exact Astra reference file, Sentinel
image, trained base-colour model, reference map provenance, CRS, origin and
bounds. Exclusions and generated placements must match the current map hash.
The source records `calibration_map_sha256` separately from the runtime map.
The lower-level detector retains strict same-map validation by default.
A new image or geographic frame needs its own calibration. The current minimum
keep score is **0.40**. Inference uses Sentinel pixels and learned statistics;
it does not read orthophotos, original tree coordinates or reviewed rectangle
coordinates. Scores are not calibrated probabilities. These are approximate
10 m pixel-centre proposals, with 12 m spacing and asset-default dimensions.
Lawns and shrubs can match tree features, and genuine trees can be missed.

The lower-level detector is offline and reads the published map terrain. It
never rebuilds the town or modifies source forests, maps, imagery or the default placements.
The default destination is `output/satellite_trees/sentinel-astra-tuned2/`:

- `trees.instances.json`: viewer-compatible placements with map/asset hashes.
- `candidates.geojson`: longitude/latitude points for GIS inspection.
- `report.json`: counts, settings, model provenance and separate exclusion and
  colour-reference fingerprints.
- `review.png`: Sentinel image, exclusion/training masks and candidate overlay.

Outputs are staged and validated before publication; an ordinary publication
failure restores the last good set. Input fingerprints are checked again
before replacing outputs. `--config`, `--map`, `--image`, `--tree-asset`,
`--recall-profile` and `--precision-profile` select other matching local inputs.

Preview with the same exclusion forests, using explicit viewer file overrides:

```bash
./build/godollo_viewer output/godollo.glb \
  --trees output/satellite_trees/sentinel-astra-tuned2/trees.instances.json \
  --forests forest_patches_astra.json
```

The research detectors, comparison UI, negative examples and calibration/audit
recipes are preserved on the local `experiments/satellite-tree-detection` branch.
Generated data and reports under `data/` and `output/` remain ignored local files.
