# Suggested viewer optimization tasks

These tasks include implemented work and proposed follow-ups. Implement and
measure follow-ups separately. Preserve gameplay, stable zombie IDs, spatial-query
results, and the existing corpse ownership/invalidation contract.

Already implemented: an explicit corpse-ID list for door activation,
synchronization and counting; incremental renderer preparation for changed
zombies. See [viewer documentation](../docs/VIEWER.md) for their APIs and behavior.

Recent completed fixes from the corridor performance investigation:

- `f7f2aeb`: reject distant zombie camera groups before visibility sorting;
  preserve independent shadow visibility.
- `453dece`: retire escaped ragdolls below the loaded scene minimum Y minus
  10 m, including an escaped detached bone.
- `b9cc9d3`: independently remove escaped loose wound contents, dropped weapons
  and ordinary crates. A loose content piece does not remove its owning corpse;
  crates retain stable multiplayer IDs after their bodies are destroyed.
- `d7d38d0`: accelerate shooting rays through occupied grid strips, retaining a
  bounded scan fallback. See the [ray comparison](../docs/benchmarks/zombie-ray-grid-2026-10-28.txt).

The open work below includes the remaining investigation findings. Sections 5,
6 and 7 correspond to investigation issues #5, #2 and #3 respectively; the
section numbers here continue this backlog's existing numbering. The existing
changed-corpse synchronization and profiling follow-ups in sections 2 and 3
also remain open.

## 1. Skip unchanged gameplay spatial updates

**Status:** implemented for behavior and scenario-route updates on 2026-10-24.

The loops retain membership/bounds for unchanged living actors and refresh them
when either root or rendered transform changes, including turns in place.
Death/removal and explicit loading/population/rig rebuilds retain their existing
handling. Animation-only changes do not require conservative shooting bounds to
be recomputed. Stationary locomotion also skips unused facing trigonometry.

The [evaluation](../docs/benchmarks/zombie-route-progress-2026-10-24.txt) separates
the paused-actor saving from the smaller effect on a moving horde. Persistent
facing caching was tried and rejected because its measured benefit was noise.

Start in [zombie_behavior.cpp](../src/zombie_behavior.cpp),
[zombie_scenario_routes.cpp](../src/zombie_scenario_routes.cpp),
[zombie_shooting.cpp](../src/zombie_shooting.cpp), and
[zombie_spatial_index.cpp](../src/zombie_spatial_index.cpp).

## 2. Synchronize only corpses changed by physics

**Priority:** next if settled corpses dominate CPU cost.
**Difficulty:** moderate. **Target:** CPU physics-to-render synchronization.

Synchronization now iterates only corpse IDs, but still checks every corpse's
physics revision. Sleeping corpses avoid pose reconstruction, yet remain in
that revision-check loop.

Suggested work:

- Extend existing body movement-event tracking to collect changed corpse IDs.
- Deduplicate events from multiple bones and fixed substeps, then synchronize
  each affected corpse once after physics stepping.
- Preserve the final movement event when a body goes to sleep. Ordinary
  collisions and impulses must naturally resume updates when it wakes.
- Integrate owning APIs for teleport/reset operations and destruction. Explicit
  translation must still publish its pose immediately; queued IDs must remain
  safe when a corpse is destroyed before synchronization.
- Continue notifying incremental renderer preparation and advancing palette
  revisions only when poses change. Externally driven palettes remain owned by
  their existing API.

Expected benefit: synchronization scales with changed corpses rather than all
retained corpses. Most useful with many settled bodies; little benefit when
nearly every corpse is moving.

Acceptance checks:

- Settled corpses produce no repeated synchronization work or GPU uploads.
- Final sleeping poses, collision wakeups, impulses, translation, removal, and
  multiple fixed substeps publish correct transforms and palette revisions.
- Multiple body events produce one corpse update, and removed/reused handles
  never update an unrelated corpse.

Start in [physics_world.cpp](../src/physics_world.cpp),
[zombie_ragdoll.cpp](../src/zombie_ragdoll.cpp), and
[zombie_layer.cpp](../src/zombie_layer.cpp).

## 3. Extend optimization counters and benchmark fixtures

**Priority:** alongside tasks 1 and 2. **Difficulty:** low to moderate.

`--profile` already reports behavior/preparation time and renderer instance/bounds
update counts. Extend that evidence to the remaining work:

- Count gameplay spatial updates, corpse revision checks, movement events, and
  synchronized corpses without rescanning the population to collect statistics.
- Use repeatable scenarios for mostly stationary 100,000-zombie crowds, many
  active pursuers, horizontal free-fly travel and return, awake ragdolls, and
  large settled corpse populations.
- Compare the same camera, assets, shadow settings, and simulation workload.
  Preserve asynchronous GPU timing and keep presentation/VSync outside it.
- Include the [standard 100,000-zombie open-area rendering case](../docs/BENCHMARKS.md)
  in rendering comparisons, alongside the relevant map views.
- Verify output and gameplay results as well as timings. Use synthetic fixtures
  for correctness and native GPU runs for end-to-end performance.

Acceptance: each optimization has isolated before/after CPU measurements,
appropriate regression checks, and a documented limit on where it helps.
CPU savings alone do not establish a GPU speedup or guarantee 60 FPS.

Start in [main.cpp](../src/main.cpp), [tests](../tests/), and
[viewer diagnostics](../docs/VIEWER.md#verification-and-diagnostics).

## 4. Support multiplayer populations up to 100,000 zombies

**Status:** TODO; feasibility and staged implementation.

See the [multiplayer 100,000-zombie ticket](multiplayer-100k-zombies.md) for
visibility and update-rate requirements, capacity and replication blockers,
performance risks, and acceptance checks. The behavior hearing-index rework in
`f41f836` was done mainly in preparation for this goal.

## 5. Bound static physics streaming and measure construction hitches

**Status:** TODO; investigation issue #5.
**Priority:** measure next during sustained movement. **Target:** main-thread
collider construction, retained memory and physics broadphase size.

[PhysicsScene::ensureNear](../src/physics_scene.cpp) imports full-height 16 x 16 m
map columns synchronously when props, grenades, cars, doodads or enabled physical
fists request nearby collision. New triangle batches become two mesh windings;
unique nearby trunks become capsule bodies. The imported cells, triangle/trunk
IDs, mesh data and bodies currently remain until scene destruction. Throwing an
object toward a wall can therefore import its static mesh before contact;
repeated impacts reuse it. Ordinary walking also activates nearby doors, but
does not by itself require these Box3D map meshes when no physical consumer is
present. The walking/query collision index is a separate system.

- [ ] Count imported/retained cells, triangles, trunks, bodies and mesh storage;
  time face queries, mesh construction and body/shape creation separately.
  Compare first visits with warm revisits on at least a 60-second movement route.
  Separate construction spikes from retained static-body costs; static bodies
  do not have the integration cost of perpetually falling dynamic bodies.
- [ ] Give collider chunks explicit ownership and cross-cell dependencies.
  Current global triangle/trunk deduplication assigns geometry to whichever
  cell imports it first. Simply deleting distant cell bodies can remove a wall
  or ground surface that a neighbouring retained cell still needs. Track shared
  chunk references and destroy a chunk only after its last dependency expires.
- [ ] Gather required regions from every physical consumer, including sleeping
  props, held objects, enabled player bodies, vehicles and loose contents.
  Preserve velocity lookahead. Camera visibility or player distance alone must
  not remove collision under a retained physical object. Keep ragdoll fallback
  floors and collision-category rules separate from these streamed map meshes.
- [ ] Evict unneeded cells/chunks after a grace period and a wider retention
  margin. Release mesh data after destroying its shapes/bodies, and retire
  deduplication records consistently so revisits can rebuild complete collision.
  Memory should follow current physical interest plus the warm cache, rather
  than every location visited; persistent props may legitimately pin regions.
- [ ] If measurements still show construction hitches, prefetch ahead of moving
  consumers and budget optional work. Required collision must be ready before
  the physics step; a budget must never let an object advance through missing
  walls or floors. Consider cached/prepared collider data if mesh building is
  the dominant cost, preserving source fingerprints.

Acceptance: synthetic cross-cell walls/ground and shared trunks survive partial
unload and reload without holes or duplicate contacts; sleeping and held props
retain support; fast throws/cars meet colliders in time; teleports and multiple
players prepare all required regions. Route/revisit measurements show reclaimed
unused bodies/storage and report construction spikes and steady physics time
separately. Do not regenerate the user's town to validate this work.

Start in [physics_scene.hpp/.cpp](../src/physics_scene.hpp),
[main.cpp](../src/main.cpp), [doodad_layer.cpp](../src/doodad_layer.cpp),
[vehicle_player.cpp](../src/vehicle_player.cpp),
[zombie_cavity_physics.cpp](../src/zombie_cavity_physics.cpp), and
[server world.cpp](../multiplayer/server/world.cpp).

## 6. Stop repeating unchanged activated-door work

**Status:** TODO; investigation issue #2.
**Priority:** after profiling a route through many doorways.
**Target:** door render updates and zombie/door contact preparation.

Activated door bodies persist and can sleep. However,
[DoorLayer::poses](../src/door_layer.cpp) still visits activated doors, and
[ZombieDoorCollision::prepare](../src/zombie_door_collision.cpp) performs a
zombie spatial query for every activated door every physics tick. Returning to
an idle area does not shrink that retained door set.

- [ ] Measure retained versus awake/changed doors, emitted render poses, spatial
  queries and zombie blocker creation/removal over a long traversal and return.
- [ ] Publish changed door poses from physics movement events and explicit
  owning APIs. Preserve final sleep poses, wakeups, forced/replicated angles,
  and movement across multiple fixed substeps.
- [ ] Cache conservative doorway candidate membership and invalidate it when
  relevant zombies move, arrive, die or leave, or a door is newly activated.
  Sleeping doors still need correct blockers: a moving zombie can enter an
  unchanged doorway. Door-pose changes alone are not sufficient invalidation.
- [ ] Deduplicate zombies shared by nearby doors and retain stable blocker
  ownership. Keep door angles/state persistent; do not delete collision based
  only on camera distance or make doors close when revisited.

Acceptance: idle retained doors cause no repeated pose uploads or identical
neighbour searches; moving zombies, sleeping doors, newly activated doors,
shared doorways, death/removal, manual movement and multiplayer updates remain
correct. Compare the same route and door states before/after, including many
settled doors and a few active interactions.

Start in [door_layer.hpp/.cpp](../src/door_layer.hpp),
[physics_world.cpp](../src/physics_world.cpp),
[zombie_door_collision.cpp](../src/zombie_door_collision.cpp), and
[door renderer](../src/renderer.cpp).

## 7. Restore distance scaling for wounded zombie geometry

**Status:** TODO; investigation issue #3.
**Priority:** when many visible wounded actors make geometry cost significant.
**Target:** camera-pass triangle count and GPU/CPU rendering cost.

[zombie_renderer.cpp](../src/zombie_renderer.cpp) forces camera mesh level zero
when `woundBase > 0`. Such actors bypass normal mesh LoD selection while within
the camera draw range. The earlier distance-culling fix does not remove this
cost for wounded actors that remain visible; shadow selection is independent.

- [ ] Benchmark many visible wounded living actors and corpses against matching
  unwounded populations. Record LoD counts, triangles, draw calls, CPU render
  time and asynchronous GPU time, including normal and zoomed views.
- [ ] Prepare wound-aware lower-detail geometry or an appropriate distant
  representation. Original wound masks use stable source triangle/vertex IDs;
  do not apply those IDs directly to unrelated simplified geometry.
- [ ] Preserve visible wound/severing state, cavity closures, skinning and
  multiplayer masks across LoD changes. Rendering choices must not change
  authoritative hit geometry or damage. Avoid obvious healed wounds or
  reappearing severed parts when distance/zoom changes.

Acceptance: captures at useful near/far and transition views preserve damage;
large wounded populations regain distance-dependent geometry cost. Include
camera, sun/flashlight shadows and the standard
[100,000-zombie rendering control](../docs/BENCHMARKS.md).

Start in [zombie_renderer.cpp](../src/zombie_renderer.cpp),
[zombie_wounds.hpp/.cpp](../src/zombie_wounds.hpp),
[zombie_wound_surface.cpp](../src/zombie_wound_surface.cpp), and
[zombie LoD preparation](../tools/prepare_zombie_lods.py).

## 8. Audit escaped streamed-doodad bodies

**Status:** TODO; related lifecycle gap found while tracing physics streaming.
**Target:** falling forest props that cannot reach the ordinary sleep retirement.

[doodad_layer.cpp](../src/doodad_layer.cpp) owns a separate box layer. Its current
retirement requires distance beyond 6 m, an unheld prop and a sleeping body. A
prop that escapes collision and stays awake may bypass that retirement. The
`b9cc9d3` cleanup covers ordinary crates, dropped weapons and wound contents;
it does not automatically delete this owner's bodies or persistence records.

- [ ] Reproduce an escaped branch/stone/bottle and verify whether its body and
  repeated scene-preparation work persist after leaving the streaming region.
- [ ] If confirmed, apply the same whole-collider scene-minimum-minus-10 m rule
  through the doodad owner, repairing dense body IDs and invalidating grips.
  Define a persistent removed state so cell reload/save/load cannot resurrect
  the escaped prop or promote it into another falling body indefinitely.

Acceptance: escaped awake props release bodies and constraints, survive save/
reload in their intended removed state, and do not respawn on revisits. Ordinary
moved, sleeping, airborne and held doodads retain their existing behavior.

## 9. Investigate remaining render sorting, especially ragdoll palettes

**Status:** TODO; profile before changing algorithms.
**Priority:** when many visible ragdolls have changing poses.
**Target:** dirty palette-slot ordering, packing and GPU upload overhead.

`ZombiePaletteCache::flush()` comparison-sorts dirty slot IDs to merge adjacent
uploads. Healthy actors and unchanged settled corpses do not populate this list,
so the idle 100k crowd alone cannot establish a useful optimization here.

- [ ] Measure dirty slots per asset and separate sorting, palette packing and
  upload costs. Record upload calls/bytes and completed-frame throughput.
- [ ] Compare healthy crowds, many settled corpses, a few moving ragdolls and
  many simultaneously changing ragdolls, including mixed assets and visibility.
- [ ] If sorting is material, compare radix sorting or reuse of existing order
  with the current comparison sort. Preserve dirty-slot deduplication, adjacent
  upload coalescing, pose revisions, slot reuse/growth and corpse ownership.
  Avoid redundant uploads for unchanged resident poses and corpses absent from
  every camera/light render pass.
- [ ] Preserve zombies' initial instance-array/source-ID order for downstream
  memory locality, including within asset/LoD batches. Measure consumers and
  full rendering as well as the isolated sorter before changing that order.
- [ ] Audit remaining distance sorts for town/vehicle glass and blood effects
  only where measured counts/cost justify it. Any float-key radix replacement
  must preserve back-to-front blending and each path's existing tie guarantees.
  Individual trees already use linear depth-band bucket ordering; profile their
  visibility and instance preparation separately before replacing that path.

Acceptance: correct displayed poses and palette ownership through sleep/wake,
visibility changes, removal and slot reuse; unchanged blending and captures;
repeatable improvement beyond run variation without regressions in small lists
or settled-corpse scenes. Retain asynchronous GPU timing and relevant standard
rendering controls. See the [benchmark guide](../docs/BENCHMARKS.md).

Start in [zombie_palette_cache.cpp](../src/zombie_palette_cache.cpp),
[zombie_renderer.cpp](../src/zombie_renderer.cpp),
[vehicle_renderer.cpp](../src/vehicle_renderer.cpp),
[blood_mist_renderer.cpp](../src/blood_mist_renderer.cpp), and
[tree_depth_order.cpp](../src/tree_depth_order.cpp).

## Conditional follow-up: parallel physics

**Priority:** only if awake ragdoll/door physics is a measured bottleneck.
**Difficulty:** low to enable, moderate to validate and tune.

The bundled Box3D supports worker threads; the current world uses its default
single worker. Add a configurable worker count and compare one, two, and a few
workers with substantial awake-body workloads. Keep application reads and
mutations outside the physics step, and consume movement events after the step
finishes. The final argument of `b3World_Step(..., 4)` is the substep count, not
the worker count.

Acceptance: correct contacts, hinges, sleep/wake events and corpse revisions;
measured improvement under load without an unacceptable cost in light scenes.
Living zombies are not rigid bodies, so this does not parallelize their AI.

Start in [physics_world.cpp](../src/physics_world.cpp).

## Conditional follow-up: parallel behavior and CPU preparation

**Priority:** only after removing unnecessary work and profiling the remainder.
**Difficulty:** moderate to high.

Use a persistent worker pool to process batches within the current frame:

1. Complete physics and pool activation/expiration on the owning thread. Fix
   player/time inputs and capture immutable door/collision query data.
2. Process disjoint zombies in workers, collecting changes and statistics in
   worker-local storage. Freeze pool membership during this phase.
3. Join before rendering; apply removals, shared spatial-index updates, render
   notifications, and statistics in stable order on the owning thread.

Keep OpenGL calls and GPU resource ownership on the render thread. CPU instance
and bounds preparation could also be batched if it becomes expensive again;
incremental preparation has already made that stage much smaller, so measure
before introducing workers there.

Acceptance: identical perception scheduling, movement, targeting, hearing,
shooting and displayed poses; no added frame of latency; no races or lost change
notifications; a measured saving larger than scheduling and synchronization cost.

Start in [zombie_behavior.cpp](../src/zombie_behavior.cpp),
[collision_world.cpp](../src/collision_world.cpp),
[door_layer.cpp](../src/door_layer.cpp), and
[zombie_renderer.cpp](../src/zombie_renderer.cpp).
