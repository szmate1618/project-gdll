# Set grip hand

The source is **PSX First Person Arms Free v1.1.0**, by **Drillimpact**:

- [Creator's download and CC0 declaration](https://drillimpact.itch.io/psx-first-person-arms-free)
- [Creator's original Sketchfab model](https://sketchfab.com/3d-models/psx-first-person-arms-efd731f559a14ab48e29c8a200d71788)
- [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/),
  with the complete legal text in [LICENSE.txt](LICENSE.txt).

The creator permits personal and commercial use under CC0, without attribution.
Credit is retained here for provenance. The original pair of arms has 1,176
triangles and 596 exported vertices. The current source includes the creator's
`grab.R` animation used for this static right hand.

`grip_hand.json` contains the baked, untextured hand, with most of the forearm
removed and 25 mm of wrist retained behind the source wrist joint. It contains
247 vertices and 490 triangles, including a closed wrist cap. No rig, animation,
texture, or UV data is needed at runtime. Flat normals are rebuilt from the
triangles. The green preview uses 50% opacity.

The palm-heel surface near the wrist is the origin `(0, 0, 0)`, in metres.
Local **+Z points into the held object**, **+Y points toward the knuckles**,
and **+X runs across the palm**. This contact frame is shared with placement
and saved grip data; do not recenter the mesh around its bounds.

## Rebuild offline

Normal builds need only the tracked JSON and Python's standard library:

```sh
python3 tools/build_grip_hand.py \
  --source assets/crafting/grip_hand/grip_hand.json \
  --output build/assets/crafting/grip_hand.glb
```

The GLB is generated and ignored, following the weapon-asset convention.

## Recreate the tracked mesh

Download the creator's v1.1.0 ZIP and retain it under
`data/assets/drillimpact_psx_arms/`. The ZIP SHA-256 is
`13689348acce167599db2b7190214aa4dbe914f5547f74e1877ff61361fc8cb8`.
Extract its top-level `arms_rig.glb`; its SHA-256 is
`407d6be3cfee18d8d47318d307a6e759f94ed3a1c27e2ab30dd860862b94172b`.
The preparation script checks that exact source before processing it.

```sh
blender --background --factory-startup --python tools/prepare_grip_hand.py -- \
  --source data/assets/drillimpact_psx_arms/source-v1.1.0/arms_rig.glb \
  --output assets/crafting/grip_hand/grip_hand.json
```

Preparation was verified with Blender 4.5.3 LTS. It imports the GLB at 30 fps,
bakes frame 12 of `grab.R`, keeps only the right side, joins UV seams, and trims
the wrist in the undeformed frame so a bent elbow cannot leave a detached piece.
It then caps the wrist, checks that the mesh is closed, and places the contact
origin on the inside heel of the palm. The creator's newer `.blend` is not
required. No hand geometry is generated as a substitute for the source model.

The included CC0 legal text is the verbatim SPDX `CC0-1.0` license text from
[SPDX's license data](https://github.com/spdx/license-list-data/blob/main/text/CC0-1.0.txt).
