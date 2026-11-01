# Undead dog

Start with the [approved assets and continuation handoff](HANDOFF.md) for the
three final variants, durable previews and the approved file-hash checkpoint.

Rigged undead dog by **Lowbit Lab**, downloaded from the free sample on
[PSX Horror NPCs & Monsters](https://lowbitlab.itch.io/psx-horror-npcs-monsters).
The same download is offered on the
[Free PSX Horror NPCs Sampler](https://lowbitlab.itch.io/free-psx-horror-npcs-sampler).

The creator permits modification and royalty-free use in commercial and
noncommercial games and apps. Resale or redistribution of raw models and
textures is prohibited. Keep the archive, extracted sources, and prepared models
local; `source/` and `models/` are ignored by Git. The downloaded archive's
`PSX_Horror_NPCs_and_Monsters/README.txt` contains the creator's license notes.

`palette-reference.png` is the user-supplied reference. Preparation samples the
53 surface swatches at the top, excluding the labeled emissive strip and black
padding. Every atlas RGB texel is assigned to its nearest surface swatch in
Oklab color space. Alpha and the original baked vertex shading are preserved.
This retains the original texture layout, fur detail, wounds, ribs and teeth.
The original material and nearest-neighbor sampler are unchanged.
The reusable palette is saved as [../palettes/undead.json](../palettes/undead.json),
with [PNG swatches and sampling documentation](../palettes/README.md).

The self-contained `models/undead_dog.glb` retains all 496 triangles, 23 joints,
and three clips (`idle`, `walk`, `attack`). Geometry, UVs, skeleton, inverse bind
matrices, node transforms and animation accessors remain byte-for-byte intact.
The original model uses meters, Y up, and an origin between the feet. This asset
is prepared for use; it is not wired into the town or humanoid zombie systems.

Download `PSX_Horror_NPCs_and_Monsters_FREE_Sample.zip` from either creator page
and place it under `source/`, then rebuild offline from the repository root:

```bash
.venv/bin/python tools/prepare_undead_dog.py
```

The output embeds the recolored atlas; `models/dog_atlas.png` also exposes it for
inspection. `sources.json` records archive, original model, reference, output
and atlas hashes. Subsequent runs reject changed source archives or reference
images so an upstream update cannot silently replace the prepared asset.

The same command also writes `models/undead_dog_64.glb` and
`models/undead_dog_32.glb`, with matching `dog_atlas_64.png` and
`dog_atlas_32.png`. Each 64×64 texel selects the most common full RGBA color in
its original 2×2 block; each 32×32 texel does the same in its original 4×4 block.
Ties select the first tied color in source row order. Both derive directly from
the recolored 128×128 atlas. No colors are averaged or introduced. Small details
can disappear when another color occupies more pixels in their source block.
All three variants retain the same geometry, rig, animations and sampler.

`models/undead_dog_16.glb` and `models/dog_atlas_16.png` reduce the recolored
128×128 atlas directly to 16×16 with the same block-mode rule.
`models/undead_dog_16_ribs64.glb` embeds `models/dog_atlas_16_ribs64.png`: this
16×16 base enlarged to 64×64 with nearest-neighbor replication, then overlaid
with both rib patches from the existing 64×64 atlas. The copied rectangles in
64-pixel atlas coordinates are `[0,48,16,64]` for the exposed rib mesh and
`[36,19,42,26]` for the painted torso ribs (right/bottom bounds exclusive).
The mesh UVs reference these patches on either side of the dog. Everything
outside those rectangles retains the enlarged 16×16 pixels exactly.
The exposed left-side ribs use the same bone white (`RGB 209,204,201`) as the
painted right-side ribs. Their two darker baked bone shades are replaced with
that white; flesh, blood, alpha and ordinary runtime lighting are preserved.

## Appearance variants

All three appearances use the approved 16×16 base enlarged to a 64×64 atlas,
with the restored rib patches and matching off-white ribs on both sides.

| Variant | Appearance | Self-contained model | Texture |
| --- | --- | --- | --- |
| 1 | Approved original coat | `models/undead_dog_variant_1.glb` | `models/dog_atlas_variant_1.png` |
| 2 | Slightly lighter warm-gray coat | `models/undead_dog_variant_2.glb` | `models/dog_atlas_variant_2.png` |
| 3 | Near-black coat with dark brown and charcoal highlights | `models/undead_dog_variant_3.glb` | `models/dog_atlas_variant_3.png` |

Variant 1 retains the approved texture without changes and matches the current
`undead_dog_16_ribs64.glb` alias and its atlas. Variants 2 and 3 replace coat colors with existing surface swatches from
the saved palette. Flesh and blood colors remain intact; both complete rib
patches retain the original detail and bone white **RGB 209,204,201**.
Alpha, UV layout, rig and all animations are preserved. The three appearances
share the thinner tongue geometry described below.
The explicit coat mappings live in
[src/undead_dog_variants.py](../../src/undead_dog_variants.py).

The same offline preparation command rebuilds all three named variants.
`sources.json` records each appearance's model and texture hashes and the shared
palette file's path and hash. Downloaded and prepared model/texture assets
remain under the ignored `source/` and `models/` directories under the source
license; the palette, mappings, documentation and provenance are repository inputs.

## Tongue geometry

All three named appearances and the `undead_dog_16_ribs64.glb` alias use tongues
with 50% of their original vertical thickness. The top is a sloping plane, not
a constant height. Preparation holds every top vertex fixed and moves each
lower vertex halfway upward toward that plane at its existing X/Z position.
Coordinates use the original mesh's Y-up frame in meters. Duplicate vertices
at UV/normal seams move together, preserving the closed tongue surface.

[src/undead_dog_geometry.py](../../src/undead_dog_geometry.py) selects the
tongue's complete UV island and verifies its rigid attachment to the jaw.
It updates tongue positions and normals while preserving topology, UVs, skin
weights, inverse bind matrices, materials and all animation data. Preparation
always starts from the cached original GLB, so repeated rebuilds cannot apply
the flattening cumulatively. The original source archive and the ordinary
128/64/32/16-pixel texture comparison models retain their original geometry.
`sources.json` records the shared appearance geometry preparation separately
from the texture and palette hashes.

Focused geometry regression checks use a synthetic closed tongue with a sloping
top and verify half volume, fixed top vertices, unchanged X/Z and skinning,
closed seams and updated normals:

```bash
.venv/bin/python -m unittest discover -s tests -p 'test_undead_dog_geometry.py' -v
```
