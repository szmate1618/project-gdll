# Suggested viewer optimization tasks

These are proposed tasks, not changes enabled in the viewer. Implement and
measure them separately. Preserve gameplay, stable zombie IDs, spatial-query
results, and the existing corpse ownership/invalidation contract.

Already implemented: an explicit corpse-ID list for door activation,
synchronization and counting; incremental renderer preparation for changed
zombies. See [viewer documentation](../docs/VIEWER.md) for their APIs and behavior.

## 1. Skip unchanged gameplay spatial updates

**Priority:** next. **Difficulty:** low to moderate. **Target:** CPU behavior time.

The behavior loop calls `updateSpatialMembership()` for every processed zombie,
including stationary idle and attacking zombies. Grid membership already avoids
moving an ID when it remains in the same cell, but the caller still rebuilds its
conservative shooting bounds and updates the broad-phase padding limits.

Suggested work:

- Track changes to the inputs of gameplay spatial membership and shooting
  bounds. Update them only when those inputs change.
- Treat turning as a bounds change even without translation or a cell crossing.
  Keep animation-only changes separate: conservative shooting bounds already
  cover the prepared animation clips.
- Retain full rebuilds for initial loading and explicit population/rig edits.
  Keep creation, death, removal, and stable-ID membership consistent.
- Reuse the movement detection in the behavior loop where appropriate, while
  preserving distinct gameplay and rendering invalidation responsibilities.

Expected benefit: fewer per-actor bounds calculations, especially for large
active pools containing stationary zombies. Measure the saving; this does not
reduce GPU geometry or draw calls.

Acceptance checks:

- Stationary idle/attacking zombies perform no unnecessary spatial updates.
- Movement within a cell, cell crossings, and turns retain correct shooting
  candidates and bounds, including head regions extending outside a cell.
- Activation and hearing candidates match an independent reference query.
- Corpse transitions and bulk rebuilds leave no stale or missing memberships.

Start in [zombie_behavior.cpp](../src/zombie_behavior.cpp),
[zombie_shooting.cpp](../src/zombie_shooting.cpp), and
[zombie_spatial_index.cpp](../src/zombie_spatial_index.cpp).

## 2. Synchronize only corpses changed by physics

**Priority:** after task 1, or earlier if settled corpses dominate CPU cost.
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
- Verify output and gameplay results as well as timings. Use synthetic fixtures
  for correctness and native GPU runs for end-to-end performance.

Acceptance: each optimization has isolated before/after CPU measurements,
appropriate regression checks, and a documented limit on where it helps.
CPU savings alone do not establish a GPU speedup or guarantee 60 FPS.

Start in [main.cpp](../src/main.cpp), [tests](../tests/), and
[viewer diagnostics](../docs/VIEWER.md#verification-and-diagnostics).

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
