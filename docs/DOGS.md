# Undead dogs

Dogs are an independent population of up to 1,500 actors. All three
[approved appearances](../assets/undead_dog/HANDOFF.md) are selected in stable
round-robin order. Models retain their authored meter scale and feet origin.
Dogs face local +Z, matching the simulation's yaw convention.

Enable dogs in the Host Game screen, or pass `--dog-count N` to the viewer,
client (single player/hosting), or dedicated server. The default is zero.
`--dogs PATH` selects the directory containing the three approved variant GLBs;
`--no-dogs` disables them. Dogs work with `--no-zombies` in the viewer and
`--zombie-count 0` in multiplayer. Joining clients adopt the host's dog count
before loading assets and validating the content fingerprint.

Version-6 scenarios can instead declare density-weighted dog packs with their
own exact counts and spawn regions. Authored packs override the manual dog
count; scenarios without packs retain the controls above. See
[dog scenario settings](ZOMBIE_NAVIGATION_RUNTIME.md#density-placed-dog-packs-version-6).

The original corridor scenario spreads its 500 dogs over the whole town according
to family-house density; `gray-scott-corridors-2` uses 1,500 dogs with the same
density image. This does not guarantee dogs near the player's starting
point. The viewer title shows visible/total dogs. With `--profile`, the log also
reports hunting dogs, occupied scent cells and player bleeding, with separate CPU
timings for scent, dog behavior and fetch physics preparation. Those timings are
milliseconds per rendered frame; slow or background frames can contain several
physics updates. Zombie update timings exclude the dog update.

```bash
./build/godollo_viewer output/godollo.glb --fps --dog-count 1000
./build/multiplayer/godollo_client output/godollo.glb --single-player --dog-count 1000
./build/multiplayer/godollo_server output/godollo.glb --dog-count 1000
```

Restore/rebuild the licensed assets locally using the asset guide. Prepared
models, textures, and generated LoDs remain ignored under the source license.
The local content packager includes the three runtime variants and their
current LoDs by default, with model and running-animation attribution in
`credits/assets/UNDEAD_DOGS.txt`. Use `--dogs PATH` for a prepared model directory
or `--no-dogs` to omit them and disable dogs in the generated launchers.
Source archives, editing files, texture comparisons and obsolete LoDs are excluded.
The creator permits game/app use and prohibits redistribution of raw assets;
the content ZIP is for installation with the game, not a reusable model library.
Peers enabling dogs need matching approved source models in their local dog
asset directory. Missing requested models fail loading with a path-qualified
error; sessions with zero dogs do not load dog assets.

## Runtime ownership

`DogLayer` owns its instances, behavior records, health, attack events and spatial
grid. Humanoid zombie arrays, interior reservations, ragdolls, wound buffers and
network IDs contain no dogs. `viewer_dogs` is a separate CMake library.

Every authoritative dog captures its original spawn feet as a fixed territorial
home. Movement, AI resets, index rebuilds and later trips never change it.

`dog_behavior.cpp` handles idle, noise-directed looking, investigation, pursuit,
attacks, wandering and fetching. Ordinary activation is within
100 meters, perception suspends beyond 150 meters and resumes within 100 meters.
Observations use the same distance-dependent cadence (10/5/1 Hz), horizontal
100-degree field of view, three occlusion rays and remembered target position.
Dog eye height is 0.55 meters. Investigation walks at 1.2 m/s; pursuit runs at
3 m/s. Investigation selects **walk**, pursuit selects **run**, and bites take
priority over locomotion. Legacy models lacking run retain the walk fallback.
Locomotion time advances by actual displacement divided by the clip's reference
speed (1.2 m/s walk, 3 m/s run), so later gameplay speed changes proportionally
change playback without rebaking. Pursuit and investigation stop when players
leave; wandering and fetching continue independently of player presence.

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
scene collision world. `dog_movement.cpp` handles terrain support, wall sliding
and steps up to 30 cm. Rounded-foot support is retained while crossing an edge;
step probes include the contact lip so sloping treads remain discoverable. The
shared step helper checks the descending landing for upward support, including
ledges taller than the dog's 25 cm foot radius. Walls, over-height ledges and
insufficient headroom remain blocking. A separate dog grid constrains dog-to-dog
contacts.

`dog_steering.cpp` spreads moving dogs using nearby living dogs in their own
grid. Lateral separation and a stable passing side break straight queues, while
the final approach fades back to the remembered goal. Steering retains the
1.2/3 m/s speed limits, ignores dogs on other floors and leaves scene/crowd
collision authoritative. Contact clearance suppresses spreading near walls,
doors and props; it adds no visibility checks or behavior states.

Each `DogLayer` also owns its own doorway passage queues. Near a fully open
door, one dog centers on the opening while followers yield outside the panel
swing, then take their turn. Hints preserve the original behavior target and
apply to pursuit, investigation, wandering and fetching. Queue waiting does
not consume a roaming trip's stall timeout. Closing doors still block dogs;
death/removal releases requests, stopped movers expire, and stalled owners
yield. Dog dimensions set aperture clearance. The common
`doorway_guidance.hpp/.cpp` algorithm also serves humanoids, but dog grids,
requests and passage owners remain separate from all zombie crowds. A dog and
a zombie may still obstruct each other; no cross-population coordination is added.

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

### Wandering

`dog_wandering.hpp/.cpp` owns trip planning and population selection. Every
60 simulation seconds after the population's first behavior update, randomly
select 0.5% of living, present dogs without replacement. Only selected idle dogs
start wandering; busy selections are skipped. Fractional counts are rounded
probabilistically (200 dogs select one, 1,000 select five, a single dog has a
0.5% chance per minute). A delayed update selects once rather than replaying
missed batches.

Each trip chooses 3–7 legs and one random horizontal direction D. Each leg picks
a fresh distance of 100–300 meters and tests a supported endpoint with clear
line of sight from the dog's 0.55-meter eye height. Try D first, then +30°, -30°,
+45°, -45°, always relative to the original D. The first clear candidate wins.
Walk at 1.2 m/s using normal terrain support and capsule collision, then plan
the next leg. Record the home position and each reached waypoint. After the
chosen number of legs, return to the original spawn home, then idle. When hunting
navigation is available the return uses a navmesh path; without it, the saved
waypoints lead back to the fixed home using normal movement/collision.

If all candidates fail, retrace the visited waypoints home. A blocked first
leg ends at the trip origin. Three seconds without
progress toward a waypoint also triggers the return; if the return itself
stalls for three seconds, stop and idle. Visibility alone does not guarantee
walkability, and wandering does not request navigation paths.

Wandering dogs retain normal perception cadence, field of view, occlusion and
100/150-meter perception suspension thresholds. Full player sight interrupts
with pursuit, partial sight with investigation, and an audible noise with
looking. A close visible player can trigger the usual bite. Interruptions
discard the remaining trip. The server and standalone viewer own these
decisions; multiplayer clients play the existing replicated walking poses.

### Blood scent and hunting

Any actual player health loss starts a bleeding flag. `setBleeding(false)` clears
it; this does not add health drain, treatment controls or wound rendering.
Dead players do not emit scent. The standalone viewer and authoritative server
deposit one unit per bleeding outdoor player every simulation second, using the
existing three-second cached house membership. Replicas run neither scent nor AI.

`ScentGrid` owns a separate sparse 20 × 20 m X/Z concentration grid. Each second
it synchronously retains 80% in the cell, transfers 4% to each side neighbor and
1% to each diagonal, multiplies by 0.98, then adds player deposits. The 4:1 neighbor
weighting approximates circular diffusion; ascent compares scent rise divided by
20 m or 20√2 m. Values below 0.000001 are removed. One continuously bleeding
stationary player settles at approximately 400 m of detectable scent measured
between cell centers. Older peaks remain when the player moves or stops bleeding.

Idle, wandering and fetching dogs may start **hunting** from a scented cell.
Player-directed looking, investigation, pursuit and attacks take priority.
Ascent records a strictly uphill chain through the scent grid. Try its peak,
then up to two preceding cells. Each candidate must provide a supported outdoor
navmesh point inside that cell; a bounded center-first probe set may miss very
small walkable patches. Disconnected/invalid candidates fail safely. Publish
the hunt only after a path succeeds, preserving the prior trip and any carried
limb if all candidates fail. A successful hunt releases any limb being fetched.

Dogs follow the stored navmesh path at 1.2 m/s, with dog collision, spacing and
mesh coverage checks. On arrival they search using 3–7 five-meter legs, each
directly covered by the outdoor mesh. A failed leg, exhausted search or three
seconds without movement starts a navmesh return to the original spawn. Failure
to plan or complete that return ends safely in idle. Hearing or sight immediately
cancels the hunt/return route and resumes ordinary player reactions.

Initial checks are staggered by dog ID, failed hunts retry after five seconds,
and at most four dogs attempt hunt entry per step (up to three destination paths
each). Completion pauses scent attempts for ten seconds. Hunting continues
without a nearby player and does not continuously track a moving source.

`dog_hunting.cpp` owns hunt transitions, target selection and navigation setup.
It uses a dog-owned route/search controller and the existing published `.navmesh`
beside the map. Scenario zombies and dogs share immutable mesh geometry while
retaining independent IDs, routes and progress. Clients never initialize it.
Maps without a companion mesh retain ordinary dog behavior without hunting;
present invalid meshes or metadata/map fingerprint mismatches are errors.

### Fetching detached limbs

Every five simulation seconds while wandering (including its homeward return),
`dog_fetch.cpp` requests the nearest ten available detached arm/leg assemblies
within 100 meters. `zombie_detached_parts.hpp/.cpp` stores ownership metadata;
Box3D's existing broad phase performs the spatial query, followed by exact 3D
body-center distances. Intact limbs, heads, torsos and parts already carried by
another dog are excluded. Check sight from dog eye height to each candidate's
current center, at most ten rays per scan. The first visible candidate starts
the `fetching` behavior state. There is no extra body-part field-of-view test.

Fetch copies the visited wandering route and appends the exact interruption
point. Walk directly toward the part at 1.2 m/s using normal dog collision. At
pickup range (within 0.8 horizontal meters and 1.2 meters of the mouth, with
clear mouth-to-part sight), snap the complete detached assembly to the mouth,
wake it, and attach a firm point joint. A whole arm/leg retains its internal
elbow/knee joint during the snap. The grip follows the existing walking jaw
pose; no pickup animation is added. Only one dog can carry an assembly.

Walk back to the interruption point, then retrace the saved waypoints home.
At home, destroy the mouth joint, drop the part and idle. Imported scene
collision around the carrying dog supports the limb away from its corpse's
local fallback floor, including after the home drop. Carrying updates the
original corpse's physics pose and uses existing corpse-pose replication;
clients do not run dog AI or recreate the carry joint.
Compact corpse poses fall back to full precision when reconstructed joint
positions would differ by more than 5 cm, including limbs carried far from
their original parent bones.

If a target disappears, another dog picks it up, or approach stalls for three
seconds, return home without it. If the homeward route itself stalls for three
seconds, release the part and idle at the reached position. Normal player
perception and audible noise interrupt both fetch and wandering and immediately
release the carry joint. Dog death, corpse removal and layer destruction also
release the joint; generation-bearing physics IDs prevent stale targets from
referring to replacement bodies.

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
cmake --build build --target test_dogs test_dog_movement test_multiplayer_dogs test_dog_renderer -j4
ctest --test-dir build -R '^(viewer_dogs|viewer_dog_assets|viewer_dog_movement|viewer_dog_movement_godollo|multiplayer_dogs)$' --output-on-failure
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

The movement regressions cover ascent/descent, sloping treads, the 30 cm limit,
headroom, solid walls and glancing wall slides at 30/60/144 Hz. When the published
map exists at configuration time, an integration check compares the dogs’
approaches near the viewer’s `--spawn 0 0` with reachable humanoid approaches;
full building walls remain obstacles for both populations.
