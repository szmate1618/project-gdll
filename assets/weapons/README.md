# Low-poly firearms

The single-player viewer and multiplayer client use [chilly_durango's Low Poly Gun Models](https://chilly-durango.itch.io/low-poly-firearms),
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

## Eye-height positioning

`src/weapon_view_pose.cpp` derives rigid poses from the player's configured
standing dimensions (normally 1.80 m tall with eyes at 1.70 m). Shoulder width
is estimated at 0.255 times height, shoulder height at 0.82 times height, and
shoulder-to-hand reach at 0.44 times height. These are tuning estimates for
the game's adult character.
For the default body this gives approximately 46 cm shoulder width, 79 cm reach,
and shoulders 22 cm below the eyes.

The pistol's palm anchor is just below/behind its trigger, held about 57 cm
forward, 8 cm right and 20 cm below the eyes for a two-handed hold with bent
elbows. Each stocked weapon's butt plate is seated in the right shoulder pocket,
about 17.5 cm right, 22 cm down and 5 cm behind the eyes. A 25-degree bladed
stance brings the support shoulder forward about 19 cm, keeping the shotgun
pump within the left arm's reach. Positioning uses the
actual stock and trigger meshes, so a longer barrel cannot push the hands away
from the body. Weapon dimensions are preserved. The bores converge gently on
the view center at 25 m; the visual pose does not change hitscan behavior.

Poses are computed once at load time using the standing controller configuration.
Crouching and prone move the camera without rescaling the upper body or weapon.
The unscoped view represents a ready hold; scope mode keeps its existing overlay.

Each weapon's [item TOML](../../items/) records `muzzle_position` in these
model-local meters, at the center of the original barrel's front cap (the M14
body cap for the sniper, rather than the decorative heavy-barrel sleeve).
The measured positions are:

| Weapon | X | Y | Z |
| --- | ---: | ---: | ---: |
| Pistol | 0 | 0.0267731 | -0.11 |
| SMG | -0.0113252 | 0.0081202 | -0.325 |
| Sniper | 0 | 0.0205822 | -0.66 |
| Shotgun | 0 | -0.0045398 | -0.475 |

The overlay applies its cached rigid pose to this position; the scene light also
applies the inverse camera view. Successful shots trigger a warm point light
with an 8 m range and a 75 ms quadratic fade, including in scope mode. The light
affects both the scene and the weapon, without adding a shadow-map pass.

The multiplayer first-person overlay uses the server-confirmed weapon slot and
the same poses. Linux and Windows client packages bundle the generated GLBs and
this attribution under `assets/weapons/`. Headless clients and servers do not
load them. Remote player bodies currently do not carry visible weapon meshes.

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
cmake --build build --target test_weapon_models test_weapon_view_pose test_weapon_overlay
ctest --test-dir build -R '^viewer_weapon' --output-on-failure
./build/test_weapon_overlay build/weapon-overlay
./build/test_weapon_overlay build/weapon-overlay-wide 1280 720
```

The rendering check needs display/GPU access. Captures are numbered in slot order.
