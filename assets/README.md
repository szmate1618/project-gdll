The build generates `test.glb` locally with `tools/create_test_asset.py`, using
only Python's standard library. The generated binary is intentionally ignored by
Git; the reproducible source script is tracked.

Generate it manually from the repository root:

```bash
python3 tools/create_test_asset.py --output assets/test.glb
```

The sample contains a 14 by 12 meter ground plane with an embedded 8 by 8 pixel
checker texture, plus three boxes with terracotta walls and blue tops. Coordinates
are Y-up, in meters. A translated and rotated root places it near `(120, 0, -75)`;
nested rotations, nonuniform scale, an explicit matrix, reused meshes, and two
primitives on the box mesh exercise scene transforms and materials. The camera
should frame the full scene without assuming it is at the origin.

All geometry and texture pixels are created by the local script. No external
assets, downloads, or attribution are required.

## Tree and pine impostors

Generate `tree_impostor.glb` and `pine_impostor.glb` from the existing PNGs:

```bash
.venv/bin/python tools/create_tree_impostors.py
```

Visible heights default to 10 m for the tree and 14 m for the pine. Override
them with `--tree-height` and `--pine-height`; `--output-dir` changes the output
directory. Generated GLBs are ignored by Git, like the sample asset.

| Quad | Tree texture | Pine texture |
| --- | --- | --- |
| Front, XY | `tree.png` | `pine.png` |
| Side, YZ | `tree-side.png` | `pine-side.png` |
| Horizontal, XZ at half height | `tree-top-mid.png` | `pine-top-mid.png` |
| Horizontal, XZ at two-thirds height | `tree-top-two-thirds.png` | `pine-top-two-thirds.png` |

Each GLB contains four quads (eight triangles), with the original PNG bytes
embedded. Materials are double-sided, unlit to preserve the images' baked
colors, and alpha-masked at a cutoff of 0.35. Coordinates are Y-up in meters;
the origin is at the visible trunk base. Transparent padding on vertical cards
can put geometry bounds outside the specified visible height. The horizontal
pair shares one physical canvas scale, preserving the upper slice's smaller
footprint instead of independently cropping and scaling each slice.

These are visually matched generated images, not exact views or slices of a
shared 3D model. See `tree.prompt.txt`, `tree-side.prompt.txt`,
`tree-slices.prompt.txt`, and `pine.prompt.txt` for their generation and cleanup
history. The assets approximate foliage volume with intersecting cards.

Preview each standalone asset with the viewer's default free-fly camera:

```bash
./build/godollo_viewer assets/tree_impostor.glb
./build/godollo_viewer assets/pine_impostor.glb
```

The [tree placement workflow](../docs/TREES.md) references these shared assets.
The [viewer tree layer](../docs/VIEWER.md#trees) renders them with GPU instancing
and uses separate estimated trunk colliders for walking. The canopy cards do
not enter that collision world. When a tree GLB is opened as the standalone
main scene instead, the generic scene collision loader still treats its quads
as surfaces; preview standalone assets in free-fly mode.

## Animated zombies

The Polyart Zombie pack belongs in `zombies/models/`. See its
[asset documentation](zombies/README.md) for source, download, and attribution,
and the [idle crowd controls](../docs/VIEWER.md#idle-zombie-crowd) to run 1,000
instances on the generated town. Character assets remain separate from the
town GLB and tree placement manifests.
