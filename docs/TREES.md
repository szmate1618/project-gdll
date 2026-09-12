# Preparing tree placements from detector CSVs

Tree placement is a separate local-data workflow. Merge detector exports, then
convert their coordinates and pixel bounding boxes into instances of the shared
[tree or pine impostor assets](../assets/README.md#tree-and-pine-impostors).
The result is `output/trees.instances.json`; changing tree detections does not
require rebuilding the town GLB.

The JSON is intended for a later renderer integration. The current desktop
viewer does not load this placement file. These commands do not modify
`output/godollo.glb`, create a combined scene, or add tree collision geometry.

## Run the local example

Use the existing Python environment from the [project setup](../README.md#setup-and-run-on-linux),
the generated `output/godollo.glb`, and the corresponding `config.json`.
If the shared assets have not been generated yet:

```bash
.venv/bin/python tools/create_tree_impostors.py
```

Merge the local orthophoto detections:

```bash
.venv/bin/python tools/merge_tree_detections.py "/home/mateszabo/Downloads/*_o_2016/*_trees.csv"
```

Then prepare the instances:

```bash
.venv/bin/python tools/prepare_tree_instances.py \
  --image-root /home/mateszabo/Downloads \
  --source-crs EPSG:23700
```

The examined local dataset contains nine detector CSVs with corresponding
orthophotos at 0.4 m pixel spacing. Their GeoTIFF CRS metadata is an incomplete
`LOCAL_CS` named `SOCET-SET LSR Curved`; it does not establish a usable geographic
coordinate transformation. Their coordinate values are consistent with
Hungarian EOV (`EPSG:23700`) around Gödöllő. The example therefore supplies that
interpretation explicitly with `--source-crs`. This is a caller-selected CRS
override, not automatic CRS detection. Use the source provider's CRS for other
datasets.

A matching TIFF is required for each image referenced by the detections. A
nearby tile's resolution is insufficient: its transform and coordinate system
may differ. The local inputs include the completed detections for `65-242`,
`66-111`, and `66-112`, together with the other six CSV/TIFF pairs.

Both steps read local files and perform no downloads. The placement step reads
GeoTIFF metadata, including its affine transform and CRS; it does not decode
the orthophoto's image pixels. Generated CSVs and JSON files are kept under the
already ignored `data/` and `output/` directories.

## Merge detector exports

```bash
.venv/bin/python tools/merge_tree_detections.py inputs/*.csv other-inputs/ \
  -o data/processed/tree_detections.csv
```

Inputs may be explicit files, quoted glob patterns, or directories. Directories
are searched recursively for CSV files. Input files are sorted by their
resolved paths and repeated file paths are read once. The destination is
excluded from the input set, making directory-based reruns safe.

The default destination is `data/processed/tree_detections.csv` inside this
project. Output is replaced atomically after every row passes validation;
invalid input leaves a previous output intact.

Required columns are:

| Column | Meaning |
| --- | --- |
| `xmin`, `ymin`, `xmax`, `ymax` | Detection rectangle in the source image's pixel coordinates |
| `world_x`, `world_y` | Detection location in the source image's world coordinate system |
| `image_path` | Source image filename or path |

`x_min`, `y_min`, `x_max`, and `y_max` are accepted as aliases and normalized to
the names above. Coordinate values must be finite and bounding boxes must have
positive width and height. Optional nonempty `pixel_x`, `pixel_y`, and `score`
values are also checked as finite numbers.

The merge retains the union of additional columns, including `label`, `score`,
pixel coordinates, and quoted WKT geometry. It adds:

| Column | Meaning |
| --- | --- |
| `source_csv` | Absolute path to the original detector CSV |
| `source_row` | Physical line where the original record starts; normally 2 for the first detection |

Existing image paths are resolved relative to their original CSV directory.
Unresolved paths are retained with their provenance, so the placement step can
find relocated images through `--image-root`. Original provenance survives
reading or merging an already merged file.

Rows are never deduplicated by location or bounding-box overlap. Two exports
covering the same tree remain two detections unless you clean them separately.

## Prepare instances

```bash
.venv/bin/python tools/prepare_tree_instances.py data/processed/tree_detections.csv \
  --config config.json \
  --map output/godollo.glb \
  --image-root /path/to/orthophotos \
  --source-crs EPSG:23700 \
  --species tree \
  --min-score 0 \
  -o output/trees.instances.json
```

| Argument | Default | Purpose |
| --- | --- | --- |
| Positional input | `data/processed/tree_detections.csv` | Merged or individual detector CSV |
| `--config` | `config.json` | Configuration matching the selected town map |
| `--map` | `output/godollo.glb` | Published map supplying the coordinate frame and terrain |
| `-o`, `--output` | `output/trees.instances.json` | Placement JSON destination |
| `--image-root` | None | Additional directory containing source TIFFs; repeat for multiple directories |
| `--source-crs` | Source TIFF CRS | Explicit override for missing or unusable source CRS metadata |
| `--species` | `tree` | Shared asset choice: `tree` or `pine` |
| `--height-m` | Keep the asset's visible height | Explicit tree-height override in meters |
| `--min-score` | `0` | Minimum detection score, from 0 to 1 |

With the default assets, preserving visible height means 10 m for `tree` and
14 m for `pine`. The generic detector label `Tree` does not classify botanical
species; `--species` selects the asset used for this run.

Preparation retains detections whose supplied world centers fall within the
configured geographic bounding box. A crown may extend beyond that boundary
when its center is inside. Lower-scoring detections are counted and omitted;
missing scores are allowed at the default threshold, but a positive
`--min-score` requires a score on every input row. Present scores must lie
between 0 and 1.

Image lookup first tries the recorded path relative to the original CSV, then
searches `--image-root` directories recursively for the same TIFF filename.
Ambiguous filename matches fail with a request for an exact `image_path`.
Each row's TIFF metadata and coordinate consistency are checked before area
or score filtering, so missing or mismatched TIFFs cannot silently disappear
through those filters. `--source-crs` overrides every TIFF in the run; use it
only when those inputs share that source CRS.

The supplied world center is checked against the source TIFF's inverse affine
transform. Optional `pixel_x` and `pixel_y` must be provided together;
otherwise the pixel bounding-box center is used for this check. Pixel
coordinates must refer to the full TIFF, including any offsets from detector
image crops. A tolerance of 0.75 pixels accepts common pixel-center conventions
without changing the supplied world location.

The output is replaced atomically after successful processing. The command
reports input, retained, outside-area, and below-threshold counts. An input
with no retained detections produces an empty instance list and null overall
bounds.

### Ground coordinates and elevations

The source `world_x/world_y` location is transformed from the source CRS into
the map's projected CRS. Subtracting the map origin gives local east and north.
Instance positions use the existing glTF convention:

```text
X = local east
Y = local terrain elevation
Z = -local north
```

One unit is one meter. Tree bases use the actual exported terrain triangles
from the selected GLB, including its node transform and float32 coordinates.
Building roofs and road geometry are not used as the planting surface. The
script does not rely on `data/processed/terrain.npz` or a potentially newer
raster cache left by a failed town generation run.

The map's CRS, geographic bounds, and origin must agree with the configuration.
Changing the bounding box or CRS requires regenerating the town map first.
The exported map fingerprint binds the placements to the specific GLB used;
after replacing that map, regenerate its tree placements.

### Crown size and image orientation

Pixel `x` runs along image columns and pixel `y` along image rows. Their
bounding-box dimensions describe two horizontal crown extents: **width and
depth**, not tree height. The image's affine transform maps these directions
to source-world directions; they are then transformed into the map's local
frame. Rasterio documents this distinction between
[CRS and pixel georeferencing](https://rasterio.readthedocs.io/en/stable/topics/georeferencing.html).

Thus a 20 by 30 pixel detection in an orthophoto with 0.4 m square pixels
describes an approximately 8 by 12 m horizontal crown footprint. It does not
measure an 8 m or 12 m tall tree. Height remains the asset height unless
`--height-m` explicitly changes it.

Horizontal scale accounts for the asset's visible alpha bounds, so transparent
padding on the impostor cards does not shrink the intended visible crown.
Raster orientation and any shear are retained in the instance matrix.

## Placement JSON and later rendering

The JSON declares `"schema": "godollo.tree-instances"` and `"version": 1`.
Its main fields are:

| Field | Contents |
| --- | --- |
| `map` | Map path, `map_sha256`, CRS, origin, axes, bounds, and terrain grid information |
| `assets` | Shared `tree` or `pine` asset record with path, SHA-256, visible height, alpha-derived `visible_bounds_m`, full `geometry_bounds_m`, and `footprint_m` |
| `input` | Input CSV path and SHA-256 |
| `source_csvs` | Original detector CSV paths keyed by source ID |
| `rasters` | TIFF records keyed by raster ID: paths, stored and effective CRSs, override flag, affine transform, image dimensions, file size, and modification time |
| `settings` | Applied species, height, confidence threshold, and source CRS override |
| `transforms` | Units, axes, matrix layout, sizing convention, and height/placement description |
| `instances` | Individual placements referencing shared assets and source records |
| `counts` | Input, outside-area, below-threshold, and retained counts |
| `bounds` | Overall minimum/maximum geometry corners, or null for an empty layer |

Map and asset paths are relative to the JSON's directory. Source CSV and TIFF
paths are absolute provenance. The asset GLB holds the shared geometry and
textures; they are not embedded again for each detection.

Each instance includes its ID, asset reference, position, scale,
`dimensions_m` in width/height/depth order, and the transform `matrix`.
Its `source` object holds `csv` and `raster` references, original `row`,
`world_xy`, `bbox_pixels`, `label`, and `score`. IDs derive from the original
CSV path and row; repeated provenance is rejected instead of emitting duplicate
IDs. Distinct detections at the same coordinates are retained.

The matrix is **column-major and authoritative**:
a future renderer should apply it directly to the shared asset. Rebuilding a
transform from position and scalar scale components can lose raster
orientation, reflection, or shear. Do not apply those fields a second time after applying
the matrix.

Per-instance bounds contain the full transformed geometry's minimum and
maximum corners. They include transparent card padding and are suitable for
future conservative visibility culling. Visible alpha bounds describe canopy
sizing; they are not a replacement for these complete geometry bounds.

The files form a reusable placement layer, rather than baked town geometry.
Future runtime loading can instance the shared assets; a separate GLB export
can be added when a portable combined scene is needed.

## Limits

- The data supplies horizontal detection footprints, not measured tree
  heights or species classifications.
- Overlapping detections are retained; there is no spatial deduplication.
- Orthophoto and town data have different acquisition dates. Existing trees
  may also already appear in the town's ground-color texture.
- The Copernicus terrain is an approximate surface model. Tree bases match
  the exported terrain, which is not surveyed bare-earth ground.
- Impostors use intersecting alpha-masked cards. Placement preparation adds
  no renderer support, LOD switching, trunk colliders, or collision filtering.
