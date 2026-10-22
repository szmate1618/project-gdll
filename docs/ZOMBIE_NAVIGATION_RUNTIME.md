# Runtime navmesh navigation and Detour steering

The standalone viewer and authoritative multiplayer server can follow the
existing tree-free navmesh using Detour's local velocity sampler. The navmesh
branch was merged into local `master` at `dda2085`. This integration reuses the
Detour-only part of the committed local-steering experiment, without its other
controllers or benchmark harness.

## Automatic navigation

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

The standalone viewer and authoritative server load the `.navmesh` with the
same basename beside the selected `.glb`; there is no navigation flag. Missing
or invalid navigation data stops startup. A viewer with no zombie population
does not create a navigation subsystem. Multiplayer clients never load a mesh.

```bash
./build/godollo_viewer output/godollo.glb --fps

./build/multiplayer/godollo_server output/godollo.glb
```

Navigation comprises the mesh, pathfinder, route storage and Detour local
steering. Ordinary AI requests routes when chasing a visible player or
investigating a last observed player position. Idle zombies remain idle.

## Test-horde scenario

The directory `scenarios/test-horde/` contains one configuration file with
multiple named hordes:

```toml
version = 2
seed = 42

[zombies.crossing_horde]
count = 600
spawn = [-500.0, -300.0, -100.0, 300.0]
destination = [100.0, -300.0, 500.0, 300.0]

[zombies.roamers]
count = 400
```

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
whether any players have joined. Ordinary perception remains enabled: noticing
a player can replace the initial destination with investigation or chasing.
Once that happens, ordinary AI owns the destination. Zombies that reach their
initial destination stop and remain able to react to players. Death ends the
route; arrival does not select another random destination. Movement retains
ordinary terrain, door, tree and crowd constraints. Global connectivity does not guarantee physical
passage through dense trees or steep terrain, which remain outside the mesh.

The multiplayer client uses ordinary matching scene settings, including
`--zombie-count 1000` for this example (the sum of all horde counts); only the
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

Initial goals request a path. Moving goals and lack of progress trigger bounded
replanning; failed or disconnected routes stop rather than reverting to a
direct walk through geometry. A full local trunk buffer also stops the NPC and
increments an overflow counter. Death, teleport and population changes must not
reuse obsolete path progress.

## Server and client boundary

The standalone viewer initializes navigation on its `ZombieLayer`.
The server enables the same implementation from its authoritative `World`;
`World::step()` calls the shared NPC behavior update, including scenario routes
when no players are connected. Default shared multiplayer scene loading does
not initialize navigation. For scenarios, the server injects its authoritative
population loader while the scene assembles collision and asset fingerprints.

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

Focused tests cover config validation, meter/WGS84 axes, safe random spawning,
preplanned route storage, autonomous following and perception takeover. Existing
navigation, movement, crowd, server snapshot and client/server checks cover the
shared integration. Real-map startup validates loading and route preparation;
movement correctness is checked through focused simulation fixtures.
