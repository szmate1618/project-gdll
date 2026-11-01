# Approved undead dog assets

The user approved these three appearances with their final thinner tongue mesh.
The assets, palette, cached download and reproduction code are saved in this
workspace. [approved.json](approved.json) is the approval checkpoint: it records
the final model, texture, preview and palette hashes. It is a snapshot of this
accepted revision; the builder records current rebuild outputs in `sources.json`.

| Variant | Appearance | Model | Texture | Preview |
| --- | --- | --- | --- | --- |
| 1 | Original coat | [GLB](models/undead_dog_variant_1.glb) | [PNG](models/dog_atlas_variant_1.png) | [Render](previews/variant-1.png) |
| 2 | Slightly lighter | [GLB](models/undead_dog_variant_2.glb) | [PNG](models/dog_atlas_variant_2.png) | [Render](previews/variant-2.png) |
| 3 | Near black | [GLB](models/undead_dog_variant_3.glb) | [PNG](models/dog_atlas_variant_3.png) | [Render](previews/variant-3.png) |

[All three appearances](previews/comparison.png) and the
[tongue before/after close-up](previews/tongue-before-after.png) are saved here,
outside the build directory. The self-contained GLBs embed the texture and retain
the original 496 triangles, 23-joint skeleton, and idle/walk/attack clips.
Previews are deterministic CPU renders, using shared lighting and camera angles.

Approved design:

- Downsample the recolored 128×128 atlas to 16×16 by selecting each block's most
  common RGBA color. Ties select the first color in source row order.
- Enlarge to 64×64 with nearest-neighbor replication and restore both rib patches
  from the 64×64 texture. Both sides use off-white RGB **209,204,201** (#D1CCC9).
- The lighter and near-black coat variants use existing palette swatches. Rib
  patches and flesh/blood colors retain their approved detail.
- Tongues have half the original vertical thickness. Top vertices stay fixed
  on their sloping plane; lower vertices rise without moving X/Z. Normals follow
  the changed geometry. UVs, skinning, inverse bind matrices and clips are retained.

The reusable palette lives in [../palettes/undead.json](../palettes/undead.json),
with [PNG swatches and documentation](../palettes/README.md). The original
[palette reference](palette-reference.png) is preserved. Only the surface colors
are used by these dogs; the saved emissive strip does not enable emission.

Rebuild from the repository root using the existing environment and cached archive:

```bash
.venv/bin/python tools/prepare_undead_dog.py
.venv/bin/python -m unittest discover -s tests -p 'test_undead_dog_geometry.py' -v
```

The source archive is
`source/PSX_Horror_NPCs_and_Monsters_FREE_Sample.zip`. Its SHA-256 is pinned in
[sources.json](sources.json). Both geometry tests passed; the actual outputs were
also checked for closed tongue surfaces, half tongue volume, fixed top vertices,
palette-only colors, matching off-white ribs and unchanged rig/animation data.
Verify rebuilt models/textures against `approved.json` before using them as this
approved revision. Always rebuild geometry from the original cached source;
flattening an already flattened output would reduce its thickness again.

Relevant code:

- [tools/prepare_undead_dog.py](../../tools/prepare_undead_dog.py): orchestration,
  source/reference checks, atlas processing, embedding and output manifests.
- [src/undead_dog_variants.py](../../src/undead_dog_variants.py): coat mappings and
  shared rib-patch coordinates.
- [src/undead_dog_geometry.py](../../src/undead_dog_geometry.py): tongue selection,
  thinning and normals.
- [tests/test_undead_dog_geometry.py](../../tests/test_undead_dog_geometry.py):
  synthetic regression checks independent of the licensed source download.

Asset preparation is complete. The three variants are integrated as a separate
opt-in dog population; see the [runtime guide](../../docs/DOGS.md) for behavior,
LoDs, multiplayer synchronization and current combat scope.
Earlier resolution-comparison meshes remain in `models/`; the checkpoint above
identifies the approved final variants for continued work. See the
[asset guide](README.md) for source licensing and detailed preparation notes.

`source/` and `models/` are intentionally ignored by Git under the creator's
raw-asset redistribution restriction. Preserve these local directories. A fresh
checkout needs the cached archive restored locally or downloaded from the
creator's linked free sampler, followed by the offline rebuild command. The
palette, previews, documentation, manifests and preparation code are repository
inputs; do not force-add the raw/downloaded model or texture files.
