# Undead dogs

Dogs are an independent population of up to 1,000 actors. All three
[approved appearances](../assets/undead_dog/HANDOFF.md) are selected in stable
round-robin order. Models retain their authored meter scale and feet origin.
Dogs face local +Z, matching the simulation's yaw convention.

Enable dogs in the Host Game screen, or pass `--dog-count N` to the viewer,
client (single player/hosting), or dedicated server. The default is zero.
`--dogs PATH` selects the directory containing the three approved variant GLBs;
`--no-dogs` disables them. Dogs work with `--no-zombies` in the viewer and
`--zombie-count 0` in multiplayer. Joining clients adopt the host's dog count
before loading assets and validating the content fingerprint.

```bash
./build/godollo_viewer output/godollo.glb --fps --dog-count 1000
./build/multiplayer/godollo_client output/godollo.glb --single-player --dog-count 1000
./build/multiplayer/godollo_server output/godollo.glb --dog-count 1000
```

Restore/rebuild the licensed assets locally using the asset guide. Prepared
models, textures, and generated LoDs remain ignored under the source license;
they are not added to Git or to the default redistributable content pack.
Peers enabling dogs need matching approved source models in their local dog
asset directory. Missing requested models fail loading with a path-qualified
error; sessions with zero dogs do not load dog assets.

## Runtime ownership

`DogLayer` owns its instances, behavior records, health, attack events and spatial
grid. Humanoid zombie arrays, interior reservations, ragdolls, wound buffers and
network IDs contain no dogs. `viewer_dogs` is a separate CMake library.

`dog_behavior.cpp` mirrors the zombie perception and movement states: idle,
noise-directed looking, investigation, pursuit and attacks. Activation is within
100 meters, perception suspends beyond 150 meters and resumes within 100 meters.
Observations use the same distance-dependent cadence (10/5/1 Hz), horizontal
100-degree field of view, three occlusion rays and remembered target position.
Dog eye height is 0.55 meters. Investigation walks at 1.2 m/s; pursuit runs at
3 m/s. Investigation selects **walk**, pursuit selects **run**, and bites take
priority over locomotion. Legacy models lacking run retain the walk fallback.
Locomotion time advances by actual displacement divided by the clip's reference
speed (1.2 m/s walk, 3 m/s run), so later gameplay speed changes proportionally
change playback without rebaking. Dogs stop when players leave.

The run is an offline, automatically retargeted CC0 motion from
[Petrov_the_blind's PSX dog](https://petrov-the-blind.itch.io/3d-running-dog-low-res-psx).
Blender fits its paw trajectories to our existing leg chains with a two-link IK
solve, transfers body/head/tail rotation, removes horizontal root travel and
bakes a closed loop. The approximately 0.726-second clip is retimed for the
3 m/s reference using an estimate from low, backward-moving paws. This estimate
does not guarantee zero sliding; cosmetic cleanup remains possible. No runtime
IK is used. See the [asset guide](../assets/undead_dog/README.md#running-animation)
for provenance, reproducible preparation and the local editable Blender scene.

Terrain, building, door and prop queries use a dog-sized capsule and the shared
scene collision world. A separate dog grid constrains dog-to-dog contacts.
Bites use the authored non-looping attack, apply 10 damage once at its contact
time, and recheck range, facing and occlusion at impact. Firearm pellets choose
the nearest dog or humanoid contact; dog health and death are authoritative.
Low/high-caliber hits deal 10/20 damage against 100 health, matching the humanoid
body-shot proportions. Dead dogs use a frozen fallen pose. Quadruped ragdolls,
humanoid wound/dismemberment effects, melee hand contacts, car knockdowns and
grenade blast damage are not implemented for dogs in this first integration.

The renderer allocates separate dog animation textures, instance buffers, LoD
batches and camera/sun/flashlight lists. Only stateless animation upload and
shader utilities are shared with humanoids. Camera rendering retains the
displayed dog transforms and stepped frame for subsequent firearm reports.

## LoDs

```bash
cmake --build build --target zombie_mesh_simplify -j4
.venv/bin/python tools/prepare_dog_lods.py
```

The dog builder uses the existing meshoptimizer helper, preserving the complete
23-joint skin, all 25 scene nodes, animation data and approved textures. Each
variant has 496 / 322 / 198 / 98 triangles for base / LoD1 / LoD2 / LoD3.
Camera switches are at 20 / 45 / 90 meters, with animation sampled at continuous /
20 / 10 / 5 Hz. The camera draw distance is 180 meters; shadows use LoD1 when
available. These policies never change simulation behavior.

Content-addressed LoD files are validated before `lods.json` is replaced
atomically. Runtime loading checks source and output SHA-256 hashes and exact
rig/clip compatibility. Stale or missing LoDs fall back to the original dog mesh.
Humanoid prism LoDs are never used for dogs.

## Multiplayer

Protocol 40 adds separate dog motion and dog firearm-report lists. Every motion
sample is self-contained: dog ID, variant, feet/yaw, phase, locomotion/attack
clocks, health and state flags. Initial interest and reentry also send bounded `DogFrame` batches on the
existing reliable bulk lane, independently of humanoid setup acknowledgements.
Backpressure retains unsent dog batches; ordinary motion remains on the
unreliable essential lane.
Dog batches are capped at 256 records (approximately 9 KB), spreading initial
identities across packets and limiting fragmentation under loss. Humanoid
replication reserves the remaining message budget after dog samples are included.

Each peer owns a separate `DogReplication` cache. Dogs within 240 meters receive
20 Hz for the nearest 128 dogs within 30 meters, 5 Hz for the next interest
band up to rank 512 within 100 meters, and 1 Hz for the remaining actors.
Initial interest,
reentry, state transitions and health changes bypass cadence; oldest deadlines
prevent starvation when a batch fills. Packet loss is
repaired by reliable initial delivery and ordinary repeated self-contained samples, including settled deaths.

`DogPlayback` retains sparse samples independently of humanoid replicas. It
rejects older per-dog ticks, interpolates behind the server, caps extrapolation
at 250 ms, publishes deaths immediately and hides actors after five seconds
without a sample. Clients create hidden slots and never run dog AI or generate
placements. Matching source dog model bytes and dog population count participate
in the ordinary session fingerprint.

## Verification

```bash
cmake --build build --target test_dogs test_multiplayer_dogs test_dog_renderer -j4
ctest --test-dir build -R '^(viewer_dogs|viewer_dog_assets|multiplayer_dogs)$' --output-on-failure
./build/test_dog_renderer build/dog-render-check
.venv/bin/python multiplayer/tests/dog_loopback.py \
  build/multiplayer/godollo_server build/multiplayer/godollo_client \
  assets/undead_dog/models build/dog-loopback
```

The rendering check needs display/GPU access and writes captures of all three
variants, idle/walk/run/attack playback, eight side-view run poses, all LoDs,
and hidden/dead actors. The UDP
check starts an isolated synthetic scene with 1,000 dogs and two clients,
including a late join, with simulated lag and packet loss. It preserves small
fixtures and logs under the requested scratch directory.
