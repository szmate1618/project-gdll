# Low-poly firearms

The single-player viewer uses [chilly_durango's Low Poly Gun Models](https://chilly-durango.itch.io/low-poly-firearms),
released under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
The original author permits modification and commercial use without attribution.
The mesh source and our modified sniper are retained here under the same CC0 dedication.

| Slot | Source model | Prepared triangles |
| --- | --- | ---: |
| 1 — pistol | 1911 | 139 |
| 2 — SMG | UMP | 233 |
| 3 — sniper | Modified M14 | 354 |
| 4 — shotgun | TacticalPump (stock pump) | 151 |

The sniper retains the M14 stock, receiver, trigger, magazine and slide. Its
fore-end/barrel is extended, with a heavy barrel sleeve, two scope mounts, and
a six-sided scope with objective, eyepiece and colored lenses. One collinear
source tessellation sliver is removed from the M14 during preparation; the
asset builder still rejects degenerate triangles. Flat steel, polymer, wood
and lens colors are added locally. The meshes have no external textures.

`models.json` is the tracked, editable mesh source, including separate named
parts. Coordinates are camera-local meters: X right, Y up, muzzle toward -Z.
Source display-grid offsets are removed and each weapon is scaled uniformly;
rotations preserve handedness and outward winding. The source SHA256 is recorded
in the file. The original download is `Guns.blend` (1,383,776 bytes), SHA256
`f12a0cbdb38568908d9dba3ed4529de6257d6ba2739026fc16de15195533e93f`.

Normal CMake builds run `tools/build_weapon_assets.py` with standard-library
Python to validate the source and create four GLBs in `build/assets/weapons/`.
They need neither Blender nor network access. The GLBs remain ignored generated
outputs. The existing glTF loader reads them once at startup; the overlay batches
all parts into one draw per equipped weapon and uses depth testing for correct
self-occlusion. It renders after scene/debug geometry and clears scene depth;
only the reticle and presentation follow it. It does not add collision or shadows.
Weapon firing, inventory, scope and instant reload behavior are unchanged;
these meshes currently have static poses without arms or part animations.

To regenerate `models.json`, download the free source from the page above and
save it at `data/assets/durango_firearms/Guns.blend`, then run Blender 3.6:

```bash
blender --background --disable-autoexec data/assets/durango_firearms/Guns.blend \
  --python tools/prepare_weapon_models.py -- --output assets/weapons/models.json
cmake --build build --target viewer_weapon_assets
```

The Blender preparation script verifies the source fingerprint before extracting
meshes. The unmodified download stays separate under ignored `data/`.

Verify the CPU loader/coordinate checks and capture the four viewmodels:

```bash
cmake --build build --target test_weapon_models test_weapon_overlay
ctest --test-dir build -R '^viewer_weapon' --output-on-failure
./build/test_weapon_overlay build/weapon-overlay
```

The rendering check needs display/GPU access. Captures are numbered in slot order.
