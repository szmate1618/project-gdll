# Runtime navmesh navigation and Detour steering

The standalone viewer and authoritative multiplayer server can follow the
existing tree-free navmesh using Detour's local velocity sampler. The navmesh
branch was merged into local `master` at `dda2085`. This integration reuses the
Detour-only part of the committed local-steering experiment, without its other
controllers or benchmark harness.

## Enable navigation

Supply a precomputed cache matching the loaded map with `--zombie-navmesh FILE`:

```bash
./build/godollo_viewer output/godollo.glb --fps \
  --zombie-navmesh build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh

./build/multiplayer/godollo_server output/godollo.glb \
  --zombie-navmesh build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh
```

The retained cache above is an example local fixture, not a distributable or
automatically selected asset. Use the existing [bake and merge tools](ZOMBIE_PATHFINDING.md#bake-and-replay-input)
to prepare a cache for another scene. An explicitly requested invalid cache is
an error; omitting the option retains existing direct locomotion.

The multiplayer option is parsed only by the server executable. Do not pass it
to the multiplayer client. No client navmesh asset, navigation initialization,
movement update or network protocol change is required.

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

The standalone viewer explicitly enables navigation on its `ZombieLayer`.
The server enables the same implementation from its authoritative `World`;
`World::step()` already calls the shared NPC behavior update. The shared
multiplayer `Scene` constructor never loads or enables navigation.

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
The runtime reports the cache and replay fingerprints; it cannot infer a map
content match from that header alone. Use a cache prepared from the same scene.
Runtime clearance checks reject meshes whose body clearance is too small.
The physical collision world still checks actual accepted movement.

The existing mesh is planar: buildings and fences are baked obstacles, with
no terrain slope, indoor or moving-door connectivity model. Goals outside the
mesh or in disconnected regions stop safely. Those capabilities require later
navigation work rather than additional Detour sampler settings.

## Validation

The viewer, server and client build with the integration. The 22 focused tests
cover Detour sampling without update allocations, BVH trunk queries, corner
following at kilometre coordinates, blocked and disconnected goals, tree bypass,
existing movement and crowd constraints, server snapshots and client/server
loopback. A three-frame hardware-rendered viewer startup also loaded the real
map and navmesh successfully; movement correctness is covered by the focused
simulation tests rather than that short capture.
