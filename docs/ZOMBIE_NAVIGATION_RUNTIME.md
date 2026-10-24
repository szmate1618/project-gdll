# Scenario navmesh navigation and Detour steering

Scenario-configured zombies in the standalone viewer and authoritative
multiplayer server follow the tree-free navmesh using Detour's local velocity
sampler. Ordinary behavior zombies retain their original direct movement. The navmesh
branch was merged into local `master` at `dda2085`. This integration reuses the
Detour-only part of the committed local-steering experiment, without its other
controllers or benchmark harness.

## Map preparation and scenario navigation

Normal map generation (`python -m src.generate`) now bakes a tree-free,
four-vertex convex cache at `output/godollo.navmesh`. It extracts transformed
building footprints and individual fence panels from the exported GLB, using
the geometry approach from the earlier pathfinding benchmark. Buildings use
conservative oriented bounding rectangles; fence entrance gaps remain separate.
The verified map, cache, input JSON and metadata are staged before publication.
Bake failures preserve the previous map and navigation; replacement failures
roll back files already published. This is not a crash-safe multi-file transaction.

`--fences-only` rebuilds the cache before publishing updated fences.
`--trees-only` keeps it, and `--no-tree-preparation` still builds it.

With `--scenario`, the standalone viewer and authoritative server load the
`.navmesh` with the same basename beside the selected `.glb`. One mesh is shared
by the entire scenario population in that process; each zombie has a separate
stored path and progress. Missing or invalid navigation data stops scenario
startup. Ordinary viewer/server populations do not initialize navigation or
require a mesh. Multiplayer clients never load a mesh.

```bash
./build/godollo_viewer output/godollo.glb --scenario scenarios/test-horde/scenario.toml
./build/multiplayer/godollo_server output/godollo.glb --scenario scenarios/test-horde/scenario.toml
```

Only scenario initialization requests navmesh paths. Per-frame following and
ordinary chasing/investigation never request or retry searches.

## Test-horde scenario

The directory `scenarios/test-horde/` contains one configuration file with
multiple named hordes:

```toml
version = 3
seed = 42

[player]
spawn = [2258.579, 2258.579]
orientation = [45.0, 0.0]

[zombies.roamers]
count = 1000

[zombies.crossing_horde]
count = 600
spawn = [2400.0, 2400.0, 2900.0, 2900.0]
destination = [-2900.0, -2900.0, -2400.0, -2400.0]
```

This example has 1,000 zombies with mapwide random spawns and destinations,
plus 600 traveling from the lower-right (southeast) corner to the upper-left
(northwest) corner of the current roughly 6 km map, for 1,600 zombies total.
The player starts 200 m northwest of the crossing horde's nearest spawn corner,
facing southeast toward its spawn region.

Each `[zombies.NAME]` table requires its own `count`. Spawn and destination are
independently optional flat rectangle arrays. Omitting either allows random
valid positions across the map for that endpoint; a supplied rectangle limits
random positions to its bounds. There is no `"random"` specifier. Counts must
be positive integers, with at most 100,000 zombies across all hordes.

Rectangles use `[min_x, min_z, max_x, max_z]` in absolute runtime meters,
X east and Z south. They are relative to the map's recorded origin, independent
of the scene bounds center and identical to the `--spawn X Z` coordinate frame.
All scenario locations use meters; the `coordinates` setting is rejected.
The C++ viewer/server do not require PROJ or geographic metadata to load them.
The seed is optional; omitting it produces a new random layout each load.
Seeded loads use a stable horde-name order.

This is scenario format version 3; `version` may be omitted and defaults to 3.
Explicit older versions are rejected. To migrate a version 2 scenario already
using world meters, set `version = 3` and remove any `coordinates = "world"`.
For WGS84 bounds, first convert the region offline using Python's `pyproj` and
the published map's recorded CRS and origin: project longitude/latitude,
subtract the projected origin, then use X = east and Z = -north. Choose world
rectangle bounds around the projected region; geographic rectangles generally
do not project to axis-aligned world rectangles, so this can enlarge the region.
Remove `coordinates = "wgs84"` after conversion. Python map/asset preparation
continues to use `pyproj`.

An optional `[player]` table supplies a ground-level starting location and
orientation. Existing scenarios can omit it:

```toml
[player]
spawn = [700.0, -2600.0] # Absolute world X/Z meters, X east / Z south.
orientation = [-90.0, 0.0] # Yaw/pitch degrees; -90 north, 0 east, 90 south.
```

Both arrays are required when the table is present. Ground height is resolved by
the normal safe-player-spawn query. Yaw is in [-180,180], pitch in [-89,89], and
positive pitch looks up. The viewer enters walking mode automatically; `--spawn`
overrides the location while retaining the authored facing. The server uses the
same location/facing for joining players, with its existing small per-player
spacing. Clients take the initial facing from the first snapshot unless `--look`
is supplied. Clients must still use the same `--spawn` position and vegetation
preset as the server for the existing shared-content fingerprint.

The [old-forest damage scenario](../scenarios/old-forest-damage/scenario.toml)
contains 100 zombies, initially 100–120 m ahead of the player in the original
orthophoto forest patch `northeast_north_e+01_n+05_01`. The player starts at
world X/Z `[700, -2700]`, facing north. The horde walks south toward and past
the player, continuing to destinations 100–150 m behind it for roughly
200–270 m routes. The player and both endpoint rectangles lie inside that same
patch. Its positions are authored constants selected from `forest_patches.json`;
no forest detection runs at load.
Use the original vegetation preset:

```bash
./build/godollo_viewer output/godollo.glb --vegetation orthophoto \
  --scenario scenarios/old-forest-damage/scenario.toml
```

The old flat `[zombies]` format is rejected. To migrate, change it to
`[zombies.NAME]`, remove `spawn = "random"` and `destination = "random"`, and
replace the old spawn rectangle key with `spawn`. Old `spawn_rectangle_m`
bounds used east/north offsets: for a georeferenced map convert
`[west, south, east, north]` to world `[west, -north, east, -south]`.
For unreferenced maps, also add the old scene-bounds center to X/Z.
Old `spawn_rectangle_wgs84` bounds require the offline geographic conversion
above. Set `version = 3` after migrating either old rectangle format.

```bash
./build/godollo_viewer output/godollo.glb --scenario scenarios/test-horde/scenario.toml
./build/multiplayer/godollo_server output/godollo.glb --scenario scenarios/test-horde/scenario.toml
```

The scenario supplies the population instead of the ordinary park/radial
placement. Spawns must be on safe terrain inside the mesh and any authored
spawn rectangle, with no body overlap even between hordes. Unbounded spawns
sample navmesh area across all components. Destinations must be safe points in
the spawn's connected navmesh component and inside any authored destination
rectangle. Unbounded destinations sample that entire component by area.
Sampling has bounded retries; insufficient space, an unreachable destination
region, failed path searches or exhausted route storage stop loading with an
error identifying the affected horde when endpoint sampling fails.
All paths are calculated before the population begins simulation.

Scenario zombies follow their initial routes regardless of player distance or
whether any players have joined. Original perception remains enabled: noticing
a player can permanently hand the actor to original investigation/chasing.
That handoff releases the scenario path. Original behavior uses direct movement
with the existing physical collision and crowd guards, without navmesh queries
or Detour avoidance. It cannot reactivate scenario travel, even after the
behavior becomes idle, expires or observes another player. Zombies that reach their
initial destination stop and remain able to react to players. Death ends the
route; arrival does not select another random destination. Movement retains
ordinary terrain, door, tree and crowd constraints. Global connectivity does not guarantee physical
passage through dense trees or steep terrain, which remain outside the mesh.

The multiplayer client uses ordinary matching scene settings, including
`--zombie-count 1600` for this example (the sum of all horde counts); only the
server receives `--scenario`. The server sends
positions through existing snapshots. Client sources and the protocol are
unchanged. Small synthetic maps under 100 meters retain the client's existing
zero-population rule; use town-sized maps for multiplayer scenarios.

## Ownership and movement

`ZombieNavigation` owns the immutable mesh, the existing independent weighted
search (`g + 8h`, 50,000 search records), the exactly 10,000,000-byte waypoint
arena and an index from stable NPC IDs to path state. The layer enables it only
after its population is loaded. Each NPC keeps its own route progress and actual
accepted velocity. Navigation and Detour buffers are allocated during setup.

The waypoint follower validates corner shortcuts against the mesh. Nearby trunk
circles come from the collision world's existing BVH into a preallocated
256-entry buffer. Detour uses those stationary circles and the NPC's previous
accepted velocity to select a local velocity. The selected step must remain in
the navmesh. Nearby actors use the existing full scene/terrain/door/crowd guard;
distant scenario travelers use the swept approximation described below. Accepted
movement feeds the next steering update.

`setScenarioDestinations()` performs the cold path requests once.
`ZombieNavigation::follow()` follows the stored goal and path; it cannot start a
search. Stalls and tree/crowd obstruction retain that path without replanning.
If a double-precision portal corner cannot be reached faithfully by float body
positions, the follower tries nearby representable points with covered incoming
and outgoing connections. Away from corners, it can reconnect forward onto the
same stored path segment after local avoidance. This is bounded movement on the
existing path, not a new search, teleport or looser mesh tolerance. An exact-key
memo avoids repeating that immutable-mesh calculation at an unchanged position;
position, segment, speed or timestep changes recompute it, and cold path
replacement/reset/release invalidate it.

A full local trunk buffer stops the NPC and increments an overflow counter.
Death releases the path. Teleports, uncovered positions and population changes
invalidate stale routes and stop travel rather than silently searching again.

The fixed destination polygon and generation-checked path handle are retained
from planning. The follower reuses the current polygon before locating a new
one, and calculates final-goal distance only when the final leg needs it.
Coverage first tests
both endpoints against the cached current convex polygon; boundary-crossing
segments retain the full spatial/interval fallback. Exact containment does not
relax mesh boundary tolerances.

Scenario following also certifies a small rectangle inside one convex polygon:
all four corners must be covered, preferably extending 60 cm ahead with 2 cm
padding. Any subsequent segment with both endpoints inside that rectangle is
covered, even if steering/crowd sliding changes it. Outside the rectangle, use
the normal coverage check. A covered current position also reuses the certified
polygon. These are geometric proofs, not reduced-frequency optimistic checks.
The existing exact-match step cache still avoids duplicate physical validation;
accepting movement clears that one-step result, while certified regions remain
valid until exit or route invalidation. Reset/replan/release clear the region.

Behavior and scenario loops update gameplay spatial membership and shooting
bounds when the root or rendered transform changes, including turns in place.
Unchanged living actors retain their existing membership. Death/removal/invalid
asset handling and explicit population/rig rebuilds retain their existing
invalidation paths. Stationary locomotion skips unused facing trigonometry;
moving actors retain the original rotation calculation.

Steering also reuses at most 32 static trunk candidates in a padded 3D patch.
Every call still filters the current height/range in original BVH order;
world changes or patch exit refresh it, and dense overflow uses the full BVH.
The path follower rejects obviously distant arrival tests before `hypot`, with
the original calculation retained around arrival/reached boundaries.

The shared crowd keeps exact current occupancy counts in 8 m sectors. A sweep
whose entire expanded fine-grid query lies inside a sector containing only
itself can return the unobstructed endpoint directly. Other sweeps retain the
fine-grid solver, ID ties and budget. Counts change on commit/removal; this is
an immediate occupancy proof, not a timed cache of neighbors. See the [route,
crowd and pose experiments](benchmarks/zombie-route-efficiency-2026-10-24.txt).

Each scenario route owns a bounded walking-ground cache for a 1 m square patch.
It keeps up to 16 static support candidates across all heights, re-evaluating
triangle coverage and the current height limits on every query. Leaving the
patch or changing collision worlds refreshes it; overflowing patches use the
full BVH. Live box support is always queried at its current pose. These caches
preserve the existing slope, step, contact and crowd checks.

Movement streams contacts to select the first blocker or deepest blocking
contact, retaining the existing collision predicate and deterministic tie key.
The legacy sorted contact-list API remains for other callers. Each scenario
route also caches up to 64 static triangle and 32 trunk candidates in a padded
3D patch. Reuse requires the complete current query bounds to remain covered;
world replacement or patch exit refreshes it, and overflow uses the full BVH.
The walking query also caches conservative maximum triangle heights clipped to
that patch. If every possible static contact is provably harmless support, it
skips the triangle tests while retaining trunk/live-object work. Height bounds,
body radius, wall padding and float-coordinate margins must prove an upward
normal above the existing support threshold; uncertain patches use exact tests.
The complete contact-list API retains every contact for other callers.
Candidate refresh also certifies
whether the full patch is outside all potential door hinge sweeps. A certified
empty patch skips live door lookup until patch exit/world replacement; patches
near doors query current poses. Live props remain separate. This does not assume
navmesh routes are clear of open doors. Full movement retains ground support,
10 cm movement segments and crowd guards.

Successful ground lowering reuses its exact clear contact query for the
immediately following resolution query with the same skin. It still queries
the supporting height every movement step, including offscreen travelers.
Triangle contacts use a guarded face-interior fast path; edges, vertices and
plane crossings retain the general calculation. See the [contact and ground
experiments](benchmarks/zombie-contact-efficiency-2026-10-24.txt) for validation,
rejected alternatives and measured limits.

Original behavior movement has no navigation argument. Scenario following is
implemented in `zombie_scenario_routes.cpp`, with distant travel isolated in
`zombie_travel.hpp/.cpp`. Both retain shared ground, crowd and locomotion pose
updates. Perception owns the one-way handoff.

### Distant scenario travelers

Beyond 200 horizontal meters from every player, scenario travelers may refresh
steering and a short movement segment at 10 Hz. They return to full movement
within 160 m of any player; the gap prevents rapid switching. With no players,
all scenario travelers qualify. Initial segment durations are staggered by stable
ID. Application steps longer than 50 ms use full movement instead of a large
catch-up update. Ordinary behavior zombies always use their original movement.

Only the expensive segment refresh slows down. The authoritative feet advance
smoothly every frame; rendering, shooting, perception, ragdoll handoff and server
snapshots all use that same current position. Ground height and crowd sweeps
remain current. Every accepted step still receives navmesh coverage validation.
These segment refreshes follow the existing path; they never search for a path.

The segment guard uses conservative horizontal wall segments and trunk circles.
Wall segments include the third projected triangle vertex and building padding;
trunks use a swept radius test. Potential door sweeps, nearby live props, unusual
geometry and overflowing candidate patches trigger the existing full solver.
Ground support must remain terrain with the existing slope limit. Sharp height
changes outside the certified endpoint envelope, crowd interference and rejected
coverage discard the segment and retain full updates for a short cooldown.
World replacement, external feet changes, looking reactions, mode changes and
ordinary-behavior takeover invalidate pending segments.

This is a distant movement approximation. Steering decisions may differ near
trees/corners, and a moving or newly spawned prop can enter a segment before
its next refresh (up to 100 ms). Nearby-player interactions retain full checks.
Ground heights are not replaced by stale samples or interpolated terrain heights.

A zero-length proposed distant segment falls back to ordinary frame-sized
movement instead of caching a stationary wait. When a frame contains several
segment slices, the complete start-to-final chord must also remain covered;
individually covered legs do not prove that chord safe around a corner.
The [route-progress evaluation](benchmarks/zombie-route-progress-2026-10-24.txt)
records these fixes and the rejected cheaper-ground, facing-cache and longer
segment experiments. The retained refresh interval remains 100 ms.

For a viewer comparison using the full movement model everywhere:

```bash
./build/godollo_viewer output/godollo.glb \
  --scenario scenarios/test-horde/scenario.toml --full-scenario-movement
```

The owning C++ layer exposes `setDistantScenarioTravelEnabled(false)` for the
same comparison on a server or test fixture. The [experiment report](benchmarks/zombie-distant-travel-2026-10-24.txt)
records the independently measured navigation/contact changes, rejected
standalone modes, quality limits and selected combined version.

## Server and client boundary

The scenario loader initializes navigation on its `ZombieLayer` in either the
standalone viewer or authoritative server. `World` does not load a mesh or
create routes: it advances the already prepared scenario population alongside
original behavior actors. `World::step()` calls the shared update, including
scenario following when no players are connected. Default shared multiplayer
scene loading does not initialize navigation. For scenarios, the server injects
its authoritative population loader while the scene assembles collision and
asset fingerprints.

The existing build links the NPC library into both client and server, and its
shared headers remain visible to both. This is build coupling: the multiplayer
client applies server snapshots and does not run NPC navigation or avoidance.
No files under `multiplayer/client/` are changed for this integration.

## Scope and cache identity

Trees remain outside the global navmesh and are handled locally. This is the
standalone Detour obstacle-avoidance sampler, not full DetourCrowd, shared group
routes, new NPC-to-NPC avoidance, or runtime obstacle carving. Existing crowd
collision constraints continue to apply. Local avoidance cannot guarantee
escape from a tree cluster that blocks a globally selected corridor.

The mesh records the SHA-256 of its input replay JSON, not the loaded map GLB.
Generated `godollo.navigation.json` and `godollo.navmesh.json` also record the
exact source GLB SHA-256, and `report.json` records navigation provenance.
The runtime reports the cache and replay fingerprints; it cannot infer a map
content match from that header alone. Use the cache published with the map.
Runtime clearance checks reject meshes whose body clearance is too small.
The physical guard remains independent of navmesh coverage; distant travelers
use the swept approximation above.

The existing mesh is planar: buildings and fences are baked obstacles, with
no terrain slope, indoor or moving-door connectivity model. Goals outside the
mesh or in disconnected regions stop safely. Those capabilities require later
navigation work rather than additional Detour sampler settings.

## Validation

`--profile` prints a separate zombie-update breakdown for ground, contacts,
navmesh coverage, Detour steering and spatial updates. See the
[real-map profiling harness](BENCHMARKS.md#real-map-zombie-update-substeps) for
fixed-step runs and instrumentation-overhead controls.

When profiling scenarios, distinguish nearby behavior-pool entries from actors
following scenario routes: travel continues outside player activation range and
camera visibility. On tested integrated hardware, its CPU load also increased GPU
elapsed time for unchanged rendering. Read the [GPU timing caveat](BENCHMARKS.md#interpreting-gpu-time-under-cpu-load)
and linked controlled report before attributing that increase to GPU animation,
additional geometry or per-frame path searches.

Focused tests cover config validation, rejection of geographic/old formats,
world meter axes, safe random spawning, preplanned route storage,
autonomous following without runtime searches,
one-way perception takeover and ordinary behavior isolation. Existing movement,
crowd, server snapshot and client/server checks cover the shared integration. Real-map startup validates loading and route preparation;
movement correctness is checked through focused simulation fixtures.
