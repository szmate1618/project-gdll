# Approximate woodland polygon configuration

[forest_patches.json](../forest_patches.json) is a UTF-8 JSON configuration of
woodland visually traced from the satellite texture embedded in the published
level. It includes forests, smaller wooded patches, and the wooded portion of
the palace park. The viewer loads it as a separate runtime layer on a matching
georeferenced map. Neither the town GLB nor the generator output is modified.

## Runtime rendering

```bash
./build/godollo_viewer output/godollo.glb
# Select another configuration, or disable only the runtime forests:
./build/godollo_viewer output/godollo.glb --forests forest_patches.json
./build/godollo_viewer output/godollo.glb --no-forests
```

At scene load, the viewer creates polygon-shaped forest boxes with canopy tops
and side walls, following the published terrain. Concave boundaries and holes
are preserved. Two shared [generated forest textures](../assets/forests/README.md)
repeat in metric UV coordinates with mipmaps. These volumes are rendering
geometry; they never become walls or ground in the collision world.

Normal view uses individual trees when the eye is within `detail_distance_m`
of a patch's conservative bounds (350 m by default), and boxes beyond that.
Both right-click scope modes force individual trees for visible patches at any
distance. Frustum culling still uses spatial indexes. Each patch draws either
its volume or its generated trees in a frame; shadows use the same detail
choice but select casters independently through the light frustum.
Detected trees whose centres lie inside a forest patch follow that patch's
detail choice too; detector trees outside these polygons retain their usual
rendering behaviour.

Tree positions are calculated once during loading from a jittered lattice,
with stable integer hashing of the patch ID, configured seed, and lattice cell.
The same patch/settings/map/assets/detector placements produce the same
positions regardless of patch order, camera movement, or scope switching.
Tree height and rotation also vary deterministically. Positions exclude holes,
buildings, and centres within 3 m of detector trees. Bases sample the rendered
terrain triangles, without a second datum subtraction. Shared tree assets and
textures are reused and individual trees are GPU instanced. Estimated trunk
capsules participate in walking and sight queries at every detail level.

The optional `runtime` object contains:

| Field | Default | Meaning |
| --- | --- | --- |
| `tree_asset` | `assets/tree_impostor.glb` | Calibrated, unlit, masked tree GLB |
| `side_texture` | `assets/forests/side.png` | Repeating wall image |
| `top_texture` | `assets/forests/top.png` | Repeating canopy image |
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
explicit `--forests FILE` reports it as an error. The map checksum and frame
must match the recorded provenance. Forests are generated from approximate
canopy polygons and can still cross narrow roads or small unmapped clearings.

The boundaries are approximate, with uncertainty of tens of metres. They are
not surveyed forest boundaries or a complete tree inventory. Major fields,
built areas, and the northeastern motorway were excluded during tracing, but
small roads, clearings, and individual buildings may remain. Region confidence
is a qualitative visual judgement. Edge regions stop at the terrain boundary.

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
imagery date, scene ID, resolution, and pixel-edge affine transform. The cached
texture was checked against the decoded terrain image inside the GLB before
tracing. A consuming program should check this provenance and the coordinate
frame before applying the polygons to another or regenerated map.

`subdivision` describes clipping against a grid anchored at local `[0, 0]`.
Coordinates are rounded to 0.001 m for consistent shared vertices; this
numerical precision does not imply matching geographic accuracy.

A visual review overlay is available in `build/forest_patches_preview.png`
for this preparation run. Its numbered regions follow the order of `regions`
in the JSON; coloured internal lines show the subdivision boundaries.
