# Small asymmetric dry tree

`front.png` supplies the entire leaning trunk and sparse bare branches, with a
root deliberately right of center and an upper trunk bending farther left.
`branches.png` supplies only an upper branch assembly. Both are generated
photographic cutouts with darker weathered bark and preserved transparent alpha.
See [generation prompts](generation.prompt.txt) for provenance and reproduction.

Build and preview from the repository root:

```bash
.venv/bin/python tools/create_dry_tree_impostor.py
./build/godollo_viewer assets/dry_tree_impostor.glb
```

Visible height defaults to 5 m (`--height` overrides it); `--output` selects a
different GLB destination. Generated GLBs are ignored by Git. The builder embeds
both original PNGs, with two double-sided, unlit, alpha-masked quads (four
triangles). Coordinates are X east, Y up, Z south, in meters, with the origin at
the visible root base. Full transparent canvas margins remain in geometry bounds.

[layout.json](layout.json) records hand-selected image-pixel anchors, measured
from the top-left. The main card lies in XY. Its `rootPixels` maps to the origin;
its `branchJoinPixels` lies on the upper trunk where it turns sideways. The
perpendicular branch card lies in YZ at that join's **offset X position**, rather
than at the root. Its `joinPixels` maps to the same point in space, and its scale
aligns the highest twig with the main card's highest twig. It has no lower trunk
or second root. Image right on that card maps toward -Z.

The views are visually matched generated images, not projections of a shared
3D model. At exactly 90 degrees to the main card the lower trunk becomes
edge-on and disappears; the branch card supplies crown depth only. There are no
horizontal canopy cards.
The runtime [old-forest layer](../../docs/FORESTS.md#old-forests-shrub-understory-and-dry-trees)
adds instances at 3.5–6 m height, preserving the bark color and using separate
approximate trunk colliders. The four-card `tree`/`pine` placement calibrator
does not accept this asset's separate `godollo.dry-tree-impostor` metadata.
The builder does not modify map or placement files. Preview the standalone GLB
in free-fly mode: generic scene collision treats its quads as surfaces.
