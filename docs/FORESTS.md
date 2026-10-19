# Approximate woodland polygon configuration

[forest_patches.json](../forest_patches.json) is a UTF-8 JSON configuration of
conservative dense-canopy forest interiors reviewed against the downloaded
2016 orthophotos, with the original 2025 satellite footprints retained as an
additional intersection constraint. Ambiguous patches, sparse stands, park
lawns and mixed industrial/residential strips are deliberately omitted.
The viewer loads it as a separate runtime layer on a matching
georeferenced map. Neither the town GLB nor the generator output is modified.

The current set contains 9 regions and 48 patches, covering approximately
2.454 km². It deliberately covers much less than the initial 9.197 km² satellite
approximation. The [review traces](../assets/forests/conservative_canopy_traces.json)
record local east/north polygon cores, clearing exclusions, track centrelines,
and the refinement parameters. Reviewed cores are inset 20 m and intersected
with the previous region footprints inset 10 m. Mapped clearings receive an
additional 10 m exclusion margin; selected tracks receive an 8 m half-width
exclusion. Components below 2,500 m² and resulting grid fragments below 2,000 m²
are omitted. These choices favor forest interiors over complete coverage.

The six covering orthophotos have 0.4 m source pixel spacing and were reviewed
in a north-up 1 m mosaic in the published map frame. Their incomplete
`LOCAL_CS` metadata requires the explicit `EPSG:23700` interpretation already
used by the [local tree-placement workflow](TREES.md). The config records this
override, source tile names, transforms, sizes and SHA-256 checksums. These
images are older than the satellite reference, so boundaries are conservative
approximations rather than a guarantee about current ground conditions.

Review images are generated local artifacts:

- [Orthophoto comparison: previous and conservative polygons](../output/forest_polygons_orthophoto_conservative_composite.png).
- [Larger orthophoto overlay](../output/forest_polygons_orthophoto_conservative_overlay.png).

The prior configuration is preserved locally at
`data/tmp/forest_review/forest_patches_satellite_previous.json`.

## Select one vegetation source

Use `--vegetation` once to select forests and individual trees together:

| Value | Loads forests? | Forest polygons | Individual trees |
| --- | --- | --- | --- |
| `osm-tuned2` (default) | Yes | OSM | OSM trees and row samples + tuned2, deduplicated |
| `orthophoto` | Yes | Original orthophoto-reviewed polygons | Preserved orthophoto CSV detections |
| `osm` | Yes | OSM | OSM trees and row samples |
| `sentinel-astra` | Yes | Astra | Generated inside its forest polygons; no separate point dataset |
| `none` | No | None | None |

```bash
./build/godollo_viewer output/godollo.glb --vegetation osm-tuned2
./build/godollo_viewer output/godollo.glb --vegetation orthophoto
./build/godollo_viewer output/godollo.glb --vegetation osm
./build/godollo_viewer output/godollo.glb --vegetation sentinel-astra
./build/godollo_viewer output/godollo.glb --vegetation none
```

The multiplayer client and server use the same presets. Repeating the flag is
an error, including repeated identical values. Explicit presets reject the
advanced `--trees FILE` override available in the viewer and require matching
files rather than substituting another dataset. The old `original` value is
renamed `orthophoto`. The forest-file override `--forests` has been removed;
forest selection comes from the preset.

`--no-forests` is a rendering toggle: it disables forest volumes and their
generated trees while keeping individual tree instances. `--no-trees` disables
all vegetation; `--vegetation none` is the corresponding source preset.

Default preparation uses `tools/prepare_trees.py`. It retains every OSM mapped
tree and tree-row sample, suppresses tuned2 points within 6 m of them, and
publishes the combined result to `output/trees.instances.json`. The separate
OSM and tuned2 inputs are preserved. Orthophoto mode reads
`output/trees.original.instances.json` and root `forest_patches.json`.

## Optional OpenStreetMap vegetation

Prepare an independent OSM vegetation layer for the published map:

```bash
.venv/bin/python tools/prepare_osm_vegetation.py
./build/godollo_viewer output/godollo.glb --vegetation osm
```

`--vegetation osm` selects **only** `output/osm/trees.instances.json` and
`output/osm/forest_patches.json` (the `osm/` directory beside the selected map).
It does not mix in the default trees or forests. Missing, stale or invalid OSM
files are errors. Select `--vegetation orthophoto` to return to the existing data. `--no-forests`
retains just the OSM individual trees; `--no-trees` disables all vegetation.
Explicit `--trees` overrides cannot be combined with this flag.

Preparation downloads a separate cached Overpass query. Use `--offline` for a
cached extract, `--refresh` to update it, or `--input FILE.json` for a local OSM
JSON extract. Inputs can contain Overpass inline geometry or OSM API-style node
and way references with complete multipolygon members. The locally cached API
extract can be reused with:

```bash
.venv/bin/python tools/prepare_osm_vegetation.py \
  --input data/raw/osm/vegetation-api/vegetation.json
```

`natural=wood` and `landuse=forest` become runtime forest polygons, unioned to
avoid duplicate coverage and split into at-most-500 m cells. Multipolygon holes
remain clearings; unclosed or incomplete boundaries fail preparation. Park and
grass polygons alone do not imply forests. Forests use the existing deterministic
tree placement, building exclusions, collision and distance-dependent volumes.

`natural=tree` nodes place individual trees at their mapped trunk coordinates,
on the published terrain triangles. Valid `height` and `diameter_crown` tags
set their dimensions; otherwise the calibrated asset proportions are used.
`leaf_type=needleleaved` selects the pine asset. `natural=tree_row` ways generate
approximate individual positions at up to 9 m spacing, excluding candidates
within 3 m of a mapped tree or another row candidate. Row positions, unspecified
sizes and trees inside forest areas are approximations, not an inventory.

Both outputs retain source IDs/tags, OSM attribution, source and map fingerprints,
and the existing runtime schemas. Preparation leaves the map, default forest
configurations and detector tree file untouched. After rebuilding the map, rerun
preparation against the new published GLB; fingerprint validation stays enabled.

## Independent Astra satellite dataset

[`forest_patches_astra.json`](../forest_patches_astra.json) is an alternative
configuration traced by GPT-6 Astra from the cached Sentinel-2 satellite RGB
imagery only. It does not use the orthophotos or intersect with the existing
conservative cores. The original `forest_patches.json` remains the orthophoto
configuration, and the earlier satellite configuration is retained in its location
above. The alternative uses the same published map frame, fingerprint checks,
runtime assets and at-most-500 m subdivisions.

The alternative contains 22 regions and 111 patches, covering approximately
8.353 km². Its [saved traces](../assets/forests/astra_canopy_traces.json) record
GPT-6 Astra as reviewer, the satellite affine transform and hashes, canopy
outlines, clearings and selected tracks. Canopy outlines are inset 5 m;
clearings receive a 5 m margin and selected tracks a 6 m half-width exclusion.
Those margins express a tracing policy, not accuracy finer than the 10 m imagery.
Rebuild the alternative from the saved traces with:

```bash
.venv/bin/python tools/build_astra_forests.py
```

The builder checks the configured frame against the published map and validates
the recorded map, satellite and texture fingerprints before atomically
replacing the alternative. It does not download imagery or regenerate the town.

Use the alternative without replacing the existing configuration:

```bash
./build/godollo_viewer output/godollo.glb --vegetation sentinel-astra
```

Generate the comparison locally without downloads or map regeneration:

```bash
.venv/bin/python tools/compare_forests.py
```

The command writes an [interactive comparison](../output/forest_astra_comparison/index.html),
a [three-panel image](../output/forest_astra_comparison/comparison.png), and
[`metrics.json`](../output/forest_astra_comparison/metrics.json). The interactive
map embeds its satellite backdrop and works offline. It shows Original, OSM and
Sentinel-Astra separately or as outlines, with pairwise shared/source-only
coverage, opacity, zoom and pan. An optional marker layer shows mapped OSM trees
and approximate tree-row positions; those points do not contribute to forest
area metrics. Original detector-tree points are not plotted. The static image
shows the three forest sources side by side.
`--old`, `--new`, `--osm`, `--osm-trees`, `--imagery`, `--map`, and `--output` select other local inputs
and destinations. The backdrop defaults to the satellite raster recorded in
the new configuration, falling back to the processed Sentinel RGB if none is
recorded. All comparison imagery uses satellite RGB, including when
displaying the preserved orthophoto-derived baseline.

Areas are calculated from actual polygon unions with holes. Shared coverage is
their intersection; each source's exclusive coverage is its respective
differences. Subdivision boundaries do not inflate areas. The tool rejects
incompatible coordinate frames and map fingerprints before publishing its
outputs. These are differences in interpretation and source dates, not measured
forest growth or loss. Sentinel-2 RGB has roughly 10 m native spacing, so small
clearings, narrow roads and individual crowns cannot be reliably resolved.

## Runtime rendering

```bash
./build/godollo_viewer output/godollo.glb
# Select another configuration, or disable only the runtime forests:
./build/godollo_viewer output/godollo.glb --vegetation orthophoto
./build/godollo_viewer output/godollo.glb --no-forests
```

At scene load, the viewer creates polygon-shaped forest boxes with canopy tops
and side walls, following the published terrain. Concave boundaries and holes
are preserved. Two shared [generated forest textures](../assets/forests/README.md)
repeat in metric UV coordinates with mipmaps. These volumes are rendering
geometry; they never become walls or ground in the collision world.

When forests are enabled, each shared tree billboard material is tinted once
during renderer loading to match the canopy-top texture's mean linear RGB.
The calculation excludes texels discarded by the tree's alpha mask and includes
existing material color factors. It applies to detected and generated trees,
including trees outside patches, and keeps the original texture variation and
alpha silhouettes. GLBs, placement fingerprints and source images are unchanged;
`--no-forests` retains the original tree colors. Matching the average reduces
the color jump at LoD switches; the geometry and silhouettes still switch.

Individual trees also receive approximate daytime canopy shade. At renderer
load, a temporary BVH finds neighboring crowns using visible width/depth rather
than padded card bounds. Soft circular canopy proxies estimate obstructed sky
at 20% and 95% of each tree's visible height, accounting for terrain elevation
and taller neighbors. Each tree excludes itself. Nearby crowns darken lower
foliage; isolated trees and exposed tops stay brighter, with a 30% minimum
daytime brightness to retain texture detail. Proxy radii are capped at 20 m,
with influence fading to zero at three radii. This is a static approximation,
not directional sunlight visibility or a leaf-level shadow calculation.

Four floats per instance record foot height, inverse visible height and the two
brightness values. The shader blends smoothly by world height, fades this
daytime effect out with the night blend, then adds the flashlight normally.
Materials, matrices, masks and asset batching remain shared. There are no
per-frame neighbor queries, extra textures, shadow maps or render passes;
instance records grow from 64 to 80 bytes. Terrain keeps its existing tree
shadows. Distant forest volumes retain their existing shading, so the LoD
transition can still show a brightness difference. Use
`--no-tree-canopy-shading` to compare individual trees with the effect disabled;
`--no-shadows` and `--no-tree-shadows` control shadow maps independently.

Normal view uses individual trees when the eye is within `detail_distance_m`
of a patch's conservative bounds (350 m by default), and boxes beyond that.
Both right-click scope modes force individual trees for visible patches at any
distance. Frustum culling still uses spatial indexes. Each patch draws either
its volume or its generated trees in a frame; shadows use the same detail
choice but select casters independently through the light frustum.
Detected trees whose centres lie inside a forest patch follow that patch's
detail choice too; detector trees outside these polygons retain their usual
rendering behaviour.

Visibility indexes are built separately for trees outside forests and for each
patch's generated and detected trees. Each pass rejects volume-only patches and
patches outside its frustum before querying their individual-tree indexes.
The shadow pass queries the light frustum independently of the camera view.
Distant volume geometry is baked into static world-space buffers grouped by
material. Visible patch ranges use one OpenGL multi-draw submission per material
(two for the default wall/canopy materials), retaining patch culling and holes
without uploading geometry every frame. Final frame diagnostics report these
material batches; the driver may process multiple ranges within each submission.

Tree positions are calculated once during loading from a jittered lattice,
with stable integer hashing of the patch ID, configured seed, and lattice cell.
The same patch/settings/map/assets/detector placements produce the same
positions regardless of patch order, camera movement, or scope switching.
Tree height and rotation also vary deterministically. Positions exclude holes,
buildings, and centres within 3 m of detector trees. Bases sample the rendered
terrain triangles, without a second datum subtraction. Shared tree assets and
textures are reused and individual trees are GPU instanced. Estimated trunk
capsules participate in walking and sight queries at every detail level.

### Old forests, bush understory and dry trees

Loading any forest configuration also derives an old-forest polygon for each
patch. Its center is the arithmetic mean of the exterior border vertices,
excluding the repeated closing vertex. Each exterior vertex moves halfway
toward that center. This contracted polygon is stored as the old-forest region.
Vegetation placement intersects it with the original forest footprint: original
holes and concave exclusions stay clear even when contraction reaches outside
the original boundary. The visibility policy below uses the stored old region
as its independent membership boundary.

Trees in this area are 20% larger in all dimensions, including existing mapped
or detected trees; their bases stay on the terrain. The original generated-tree
lattice remains unchanged outside the old forest. A second deterministic lattice
adds 20% of the original tree density inside it, before the usual building and
detected-tree exclusions. Actual counts vary with patch shape and exclusions.
The distant volume representation retains its configured height and textures.

Each old-forest tree has a 60% chance of a bush at its base. Each of those bushes
has a 20% chance of two additional bushes sampled uniformly within a 5 m radius
disk. These additional bushes do not spawn further bushes. Every bush has its
own deterministic rotation about Y and random width of 6–14 m, with visible
height one quarter of its width (1.5–3.5 m). Candidates outside the old forest,
in holes, in buildings or without terrain support are omitted.

Bushes use two perpendicular upright cards and one horizontal card at 25% of
their height. One shared [side/top atlas](../assets/forests/bush.png) supplies
unlit, double-sided alpha masks with normal opaque-texel depth writes. Bushes
share the forest's instancing, culling, detail selection and shading. They have
no collision in either single-player or multiplayer. All placements are made
once during loading, independent of camera movement and patch order; no map or
placement-file regeneration is required.

Grenade explosions push shrubs away from the blast within a 12 m sphere around
each shrub's mid-height. The response fades with distance, rebounds gently and
settles within 0.9 seconds. `vegetation_blast.hpp/.cpp` queries a shrub-only
spatial index and shears existing instance matrices around their roots. Upright
and horizontal cards, including sun/flashlight shadows, use the same pose;
vertices, indices and draw batches are unchanged. Culling bounds include the
maximum displacement, including overlapping blasts. Source placements and
collision remain unchanged. This cosmetic effect uses event ages in both the
local viewer and multiplayer client and creates no particles.

Each patch also adds small [dry trees](../assets/dry_tree/README.md) inside its
old-forest area. The primary count is 9% of the healthy trees actually present
there, rounded to the nearest whole tree (halves round up). This includes mapped
or detected trees and both accepted generated-tree lattices; bushes, dry trees
and companions do not enter that count. Existing trees and bushes keep their
placements. For example, 100 healthy old-forest trees receive 9 additional dry
trees, before companions.

Each primary has a 10% chance of one companion 1.5–5 m away, sampled uniformly
over that annulus. Companions cannot spawn more trees. Both populations have
independent uniform 3.5–6 m visible heights and rotations through 360 degrees,
with uniform scaling preserving the asset's lean. They do not receive the 20%
old-growth enlargement or spawn bushes. Stable hashes of patch ID, seed and
primary index fix placements, heights, rotations and companion choices.

Primary positions are sampled within the contracted core and original polygon,
respecting holes, buildings, terrain support and the 3 m detected-tree exclusion.
Rejected positions are retried deterministically, up to 4,096 times per primary;
an unfillable quota rejects the load before publishing any changes. A companion
with no valid nearby position after 64 tries is omitted. Dry trees share the
forest's instancing, bounds-based culling, detail selection, lighting and
approximate root-centered trunk capsules. Bark keeps its source color rather
than receiving the green canopy tint; distant forest volumes stay unchanged.

Build the shared dry-tree asset once before loading forests on a fresh checkout:

```bash
.venv/bin/python tools/create_dry_tree_impostor.py
```

The multiplayer asset pack includes this embedded-texture GLB and the session
fingerprint covers it alongside the other shared tree assets.

The optional `runtime` object contains:

| Field | Default | Meaning |
| --- | --- | --- |
| `tree_asset` | `assets/tree_impostor.glb` | Calibrated, unlit, masked tree GLB |
| `side_texture` | `assets/forests/side.png` | Repeating wall image |
| `top_texture` | `assets/forests/top.png` | Repeating canopy image |
| `bush_texture` | `bush.png` beside the resolved side texture | Side/top alpha-mask atlas for old-forest bushes |
| `dry_tree_asset` | `dry_tree_impostor.glb` beside the resolved tree asset | Small leaning dry-tree GLB |
| `seed` | `20261011` | Unsigned 32-bit placement seed |
| `tree_spacing_m` | `9` | Lattice spacing before jitter and exclusions |
| `min_tree_height_m` | `12` | Minimum visible tree height |
| `max_tree_height_m` | `20` | Maximum visible tree height |
| `volume_height_m` | `16` | Canopy box height above sampled terrain |
| `texture_tile_m` | `32` | Horizontal wall/top texture repetition length |
| `detail_distance_m` | `350` | Normal-view switch distance from patch bounds |

Asset paths are relative to the configuration file. `--no-trees` disables both
detected trees and runtime forests; `--no-forests` retains detected trees.
`--no-tree-culling`, `--no-tree-collisions`, and `--no-tree-shadows` apply to both
tree sources. Automatic loading logs and skips incompatible configuration;
an explicit `--vegetation` preset reports it as an error. The map checksum and frame
must match the recorded provenance. Forests are generated from approximate
canopy polygons and can still cross narrow roads or small unmapped clearings.

The boundaries are approximate and are not surveyed forest boundaries or a
complete tree inventory. Obvious fields, yards, buildings and clearings were
removed during orthophoto review; very small gaps or paths covered by canopy
can remain. Region confidence is a qualitative visual judgement. Edge regions
stop inside the terrain boundary.

### Old-forest visibility contract

In the normal, unscoped view, a camera inside a stored `ForestPatch::oldForest`
region enables a **sharp 100 m horizontal visibility radius** centered on the
camera. The polygon alone determines horizontal membership: no containment in,
adjacency to, or surrounding buffer of another/new forest is required. Exact
polygon tests respect concavities, holes and excluded borders. The patch's
vertical bounds restrict activation so flying above or below the stand does not
enable it. Leaving the old region immediately restores normal visibility.
Both scope views bypass the cutoff, including for shadow casters; returning to
normal view restores it if the camera remains inside an old region.

The radius applies to individual trees, shrubs, forest volumes, town geometry,
and sun/flashlight shadow casters. Bounds crossing the radius remain eligible;
the shared scene shader clips their actual pixels at 100 m, including large
terrain/building meshes and batched roofs. Height does not reduce the horizontal
range. Nearby offscreen casters still use the light's own visibility query.
Frustum-culling debug switches do not bypass this range policy. The shared
renderer also clips its boxes, zombies and grenades; simulation, collision,
perception, damage and source placements keep their full ranges. Local and
multiplayer renderers apply the same camera-local rule without network changes.

`forest_visibility.hpp/.cpp` builds a polygon-bound spatial index once. Each
unscoped frame performs one point query and exact membership tests only for
candidate old regions. The resulting range is shared by color and shadow
queries; `visibility.hpp/.cpp` prunes distant BVH nodes before selecting draws.

**Authoring requirement:** old-forest regions explicitly opt into restricted
visibility. Construct them for dense stands where a 100 m cutoff is acceptable;
keep intended clearings and long sightlines outside those regions, using holes
in the old-region polygon where needed. Do not infer visibility safety from a
relationship with another forest region. Current vegetation placement still
clips the derived core against the source footprint, while visibility tests the
stored old-region polygon itself. New region construction must preserve this
distinction and supply the intended old-region boundaries/exclusions. Cutoff
popping and the immediate change at old-region edges are intentional; there is
no fade or automatic density/occlusion test. Clipped terrain and distant foliage
can expose the scene background through gaps between nearby trees; suitable old
region construction must account for those sightlines.


## Reading the file

Check `schema == "godollo.forest_patches"` and `version == 1` before processing.
`regions` contains labels, woodland categories, confidence, and summary areas.
`patches` contains the actual polygons to process, linked by `region_id`.

Each patch has:

- `id`: a unique identifier within this configuration.
- `region_id`: the original woodland region shared by its subdivided pieces.
- `grid_cell`: signed `[east_cell, north_cell]` indices in a 500 m grid.
- `bounds_m`: `[minimum_east, minimum_north, maximum_east, maximum_north]`.
- `area_m2`: polygon area after subtracting holes, in square metres.
- `polygon`: the exterior ring as pairs of `[east, north]` coordinates.
- `holes`: zero or more excluded interior rings, using the same coordinates.

All rings repeat their first vertex at the end. Exterior rings are
counterclockwise; holes are clockwise in the east/north plane. Adjacent patches
share edges without overlapping area. Some cells contain multiple disconnected
pieces, each represented by its own patch. Retain the small fragments at region
edges. Every patch's bounding box is at most **500 m east–west by 500 m
north–south**, including the pieces of smaller wooded regions.

This is a custom JSON schema, not GeoJSON: its coordinates are local metres,
not longitude/latitude. An example using the existing Python environment:

```python
import json
from pathlib import Path
from shapely.geometry import Polygon

config = json.loads(Path("forest_patches.json").read_text(encoding="utf-8"))
assert config["schema"] == "godollo.forest_patches" and config["version"] == 1
for patch in config["patches"]:
    woodland = Polygon(patch["polygon"], patch["holes"])
    # Use woodland for area tests or sampling; its holes exclude open areas.
```

## Coordinate frame and provenance

The file records its frame in `coordinate_frame`. Coordinates follow the
generator's local **east/north** horizontal plane, in metres. The origin and CRS
were derived through `src/config.py` and checked against the published map using
`src/tree_map.py`.

To obtain absolute projected coordinates, add `origin_projected_m` to an
`[east, north]` pair. To place a point in the viewer, use **X = east, Z = -north**.
The polygons contain no elevation; sample the published terrain at
`(east, north)` to obtain viewer Y. Do not subtract the terrain datum again.

`source` records the map and texture SHA-256 fingerprints, relative paths,
satellite imagery date, scene ID, resolution, and pixel-edge affine transform,
along with the orthophoto inputs and conservative review provenance. The cached
texture was checked against the decoded terrain image inside the GLB before
tracing. A consuming program should check this provenance and the coordinate
frame before applying the polygons to another or regenerated map.

`subdivision` describes clipping against a grid anchored at local `[0, 0]`.
Coordinates are rounded to 0.001 m for consistent shared vertices; this
numerical precision does not imply matching geographic accuracy.

The orthophoto comparison uses the original region numbers so retained regions
can be compared with the earlier satellite composite. Region IDs in the JSON
remain the authoritative identifiers; cyan lines show the 500 m subdivisions.
