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
version = 2
seed = 42

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

Each `[zombies.NAME]` table requires its own `count`. Spawn and destination are
independently optional flat rectangle arrays. Omitting either allows random
valid positions across the map for that endpoint; a supplied rectangle limits
random positions to its bounds. There is no `"random"` specifier. Counts must
be positive integers, with at most 100,000 zombies across all hordes.

`coordinates` defaults to `"world"`: rectangles use
`[min_x, min_z, max_x, max_z]` in absolute runtime meters, X east and Z south.
They are independent of the scene bounds center. For geographic bounds, use
`coordinates = "wgs84"` on the horde:

```toml
[zombies.geographic_horde]
count = 200
coordinates = "wgs84"
spawn = [19.35, 47.58, 19.36, 47.59]
destination = [19.36, 47.59, 19.37, 47.60]
```

WGS84 rectangles use `[west_longitude, south_latitude, east_longitude,
north_latitude]` in degrees. The coordinate setting applies to both rectangles
within that horde; other hordes can use a different setting. PROJ converts
samples through the loaded map's recorded CRS and origin; supplied geographic
bounds need that metadata. Projection uses local coordinate data, with network
access disabled. The seed is optional; omitting it produces a new random layout
each load. Seeded loads use a stable horde-name order.

This is scenario format version 2; `version` may be omitted and defaults to 2.
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
contains 100 zombies, initially 10–25 m ahead of the player in the original
orthophoto forest patch `northeast_north_e+01_n+05_01`. They walk south toward
and past the player's starting position, continuing to destinations 50–70 m
behind it for roughly 60–100 m routes. Its positions are authored constants
selected from `forest_patches.json`; no forest detection runs at load.
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
Old `spawn_rectangle_wgs84` bounds keep their order and need
`coordinates = "wgs84"`.

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
the navmesh. The existing scene, terrain, door and crowd movement constraints
remain the final physical guard, and the accepted movement feeds the next
steering update.

`setScenarioDestinations()` performs the cold path requests once.
`ZombieNavigation::follow()` follows the stored goal and path; it cannot start a
search. Stalls and tree/crowd obstruction retain that path without replanning.
A full local trunk buffer stops the NPC and increments an overflow counter.
Death releases the path. Teleports, uncovered positions and population changes
invalidate stale routes and stop travel rather than silently searching again.

The fixed destination polygon is retained from planning. Coverage first tests
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
Every pose still receives exact contact tests, with live doors/props queried
separately. Ground support, 10 cm movement segments and crowd guards remain.

Original behavior movement has no navigation argument. Scenario following is
implemented in `zombie_scenario_routes.cpp`; both movement modes reuse physical
collision and locomotion pose updates. Perception owns the one-way handoff.

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
The physical collision world still checks actual accepted movement.

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

Focused tests cover config validation, meter/WGS84 axes, safe random spawning,
preplanned route storage, autonomous following without runtime searches,
one-way perception takeover and ordinary behavior isolation. Existing movement,
crowd, server snapshot and client/server checks cover the shared integration. Real-map startup validates loading and route preparation;
movement correctness is checked through focused simulation fixtures.
