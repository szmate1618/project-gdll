# Tuned2 Sentinel tree preparation

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

The saved profiles are specific to the recorded scene and native Sentinel
image. A new image or scene needs its own calibration. The current minimum
keep score is **0.40**. Inference uses Sentinel pixels and learned statistics;
it does not read orthophotos, original tree coordinates or reviewed rectangle
coordinates. Scores are not calibrated probabilities. These are approximate
10 m pixel-centre proposals, with 12 m spacing and asset-default dimensions.
Lawns and shrubs can match tree features, and genuine trees can be missed.

Preparation is offline and reads the published map terrain. It never rebuilds
the town or modifies source forests, maps, imagery or original placements.
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
