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
