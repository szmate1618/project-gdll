# Palette pigeon

The bird flocks use the yellow-beige 16×16 appearance through the self-contained
[runtime descriptor](flock.json). It includes the full 710-triangle pigeon and
240/62-triangle LoDs, a 12-frame Flapping clip and the nearest-filtered atlas.
The animated wingspan is normalized to 65cm. All levels preserve the same vertex
trajectories for retained vertices. Ground idle, landing and takeoff remain in
the original GLBs; airborne flocks need only Flapping. Frame samples use local
X/right, Y/up, Z/forward meters, centered on the boid root.
See the [OpenGL flock capture](previews/runtime-flock.png) from player height
over the published town.

Rebuild the descriptor after changing the selected source appearance:

```bash
cmake --build build --target prepare_pigeon_flock -j4
./build/prepare_pigeon_flock assets/pigeon/models/pigeon_palette_16_yellow.glb assets/pigeon/flock.json
```

The tool validates temporary output before publication. Model provenance is
recorded by the descriptor's source SHA-256. Clean checkouts and packages use
the tracked descriptor directly, without running this preparation step.
Distributed attribution is in [PIGEONS.txt](../../credits/assets/PIGEONS.txt).

[Animated Bird, Pigeon](https://sketchfab.com/3d-models/animated-bird-pigeon-797d27b68af3453e865149435df6aa30)
by **Paul Spooner (dudecon / Peripheral Arbor)**, recolored using the user's
[undead palette](../palettes/README.md). The original 512×512 painted texture is
mapped to its nearest surface swatches in Oklab. UVs, mesh topology, skeleton
and the five animation clips are retained. The separate emissive swatches are
not used. Studio lighting changes rendered pixel colors; the atlas itself
contains only exact saved palette colors.

- [Standing preview](previews/pigeon_palette.png)
- [Side preview](previews/pigeon_side.png)
- [Flight preview](previews/pigeon_flight.png)
- [Prepared GLB](models/pigeon_palette.glb), with embedded PNG and animation
- [Blender scene](models/pigeon_palette.blend), with packed texture and studio
- [Palette atlas](models/pigeon_atlas.png)

The model has **710 triangles and 357 source vertices**, before export splits at
UV/normal seams, and a 136-bone skeleton. The clips are `Flapping`, `Gliding`,
`Landing`, `Standing Idle` and `Takeoff`. Blender coordinates are Z up; GLB
export converts once to glTF Y up. These source previews use CPU Cycles. The
flock integration above uses the viewer's OpenGL renderer.

## Source and attribution

Sketchfab lists **CC BY 4.0**, allowing commercial use and modification with
credit. Suggested credit: “Animated Bird, Pigeon by Paul Spooner (dudecon),
CC BY 4.0; texture recolored using the supplied undead palette.” The model page
additionally links the author's [public domain declaration](https://ip.tryop.com/).

The cached original FBX and texture came from the public
[Assimp discussion attachment](https://github.com/orgs/assimp/discussions/5970),
which identifies the same Sketchfab model. The archive contains a nested source
ZIP, the original 3,998,748-byte FBX and its 512px JPG. Fingerprints and exact
download URL are recorded in [sources.json](sources.json).

The author's currently linked `bird.blend` was also downloaded to `source/` for
reference, but evaluates to 1,420 triangles and is **not** used in this preview.
Downloaded binaries remain ignored by Git; preserve the source cache.

## Offline rebuild

From the repository root, with the cached source archive and Blender 4.5:

```bash
.venv/bin/python tools/prepare_pigeon.py
blender --background --factory-startup --disable-autoexec \
  --python tools/render_pigeon.py
```

The preparation tool verifies the archive and palette fingerprints. The render
tool imports the original FBX, applies the prepared atlas, exports the selected
bird and rig, and renders the three views. The `.blend` contains the studio
objects; the GLB contains only the bird and its rig. The GLB retains all vertex
influences, including additional influence sets beyond four weights.

## 16×16 texture comparison

The preparation tool also produces [pigeon_atlas_16.png](models/pigeon_atlas_16.png)
by nearest-neighbor downscaling of the 512×512 palette atlas. This retains exact
palette swatches without averaging. Both resolutions use nearest texture
filtering, with identical geometry, poses, cameras, lighting and render settings.

- [Standing comparison](previews/pigeon_palette_comparison.png)
- [Side comparison](previews/pigeon_side_comparison.png)
- [Flight comparison](previews/pigeon_flight_comparison.png)
- [All three comparisons](previews/texture_comparison.png)
- [16×16 GLB](models/pigeon_palette_16.glb) and [Blender scene](models/pigeon_palette_16.blend)

After the base previews exist, render and compose the smaller texture variant:

```bash
.venv/bin/python tools/prepare_pigeon.py
blender --background --factory-startup --disable-autoexec \
  --python tools/render_pigeon.py -- --texture-size 16
.venv/bin/python tools/compare_pigeon_textures.py
```

The 16×16 atlas contains 256 texels versus 262,144 in the base atlas, a 1,024×
reduction. Its PNG size and the complete animated model size have different
ratios because image compression and the rig/animation data also contribute.

## Warm off-white feathers

The [warm 16×16 atlas](models/pigeon_atlas_16_warm.png) replaces the two lightest
feather swatches, RGB 222/217/214 and 253/253/253, with the existing palette's
warm off-white **RGB 209/204/201, #D1CCC9**. Other texels retain their previous
colors. The model still uses nearest filtering and its original rig and clips.

- [Standing before/after](previews/pigeon_palette_warm_comparison.png)
- [Flight before/after](previews/pigeon_flight_warm_comparison.png)
- [All three views](previews/warm_white_comparison.png)
- [Warm GLB](models/pigeon_palette_16_warm.glb) and [Blender scene](models/pigeon_palette_16_warm.blend)

```bash
.venv/bin/python tools/prepare_pigeon.py
blender --background --factory-startup --disable-autoexec \
  --python tools/render_pigeon.py -- --texture-size 16 --warm-white
.venv/bin/python tools/compare_pigeon_textures.py --warm-white
```

## Darker yellow-beige feathers

The [yellow-beige 16×16 atlas](models/pigeon_atlas_16_yellow.png) replaces the
previous off-white (#D1CCC9) with **RGB 165/161/152, #A5A198**, an existing
surface-palette swatch. This gives the light feathers a darker, muted yellow
tint. Other texels, nearest filtering, geometry and animation data are retained.

- [Standing comparison](previews/pigeon_palette_yellow_comparison.png)
- [Flight comparison](previews/pigeon_flight_yellow_comparison.png)
- [All three views](previews/yellow_beige_comparison.png)
- [Yellow-beige GLB](models/pigeon_palette_16_yellow.glb) and [Blender scene](models/pigeon_palette_16_yellow.blend)

```bash
.venv/bin/python tools/prepare_pigeon.py
blender --background --factory-startup --disable-autoexec \
  --python tools/render_pigeon.py -- --texture-size 16 --yellow-beige
.venv/bin/python tools/compare_pigeon_textures.py --yellow-beige
```
