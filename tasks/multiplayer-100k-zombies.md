# TODO: Support multiplayer populations up to 100,000 zombies

**Status:** proposed; investigate feasibility, then implement in measured stages.

## Goal

Support up to 100,000 zombies in the authoritative multiplayer world if practical
on the target hardware. Begin with zombies randomly distributed over safe map
ground, initially idle, without a prescribed spawn region or travel destination.
Reuse the viewer's existing crowd, shared assets, spatial indexes and rendering.
This is a world-population target, not a requirement to simulate 100,000 active
pursuers or transmit/render every zombie for every player at once.

Send state primarily for zombies visible to each client, with a conservative
view margin to avoid pop-in during turns. Nearby moving/attacking zombies need
frequent updates; distant visible zombies can update less often, and unchanged
idle zombies need state on entry/change rather than repeated transforms. The
server must continue authoritative gameplay for relevant offscreen zombies.

## Preparation already completed

- [x] **Hearing index rework, commit `f41f836`.** This small hearing-system rework
  was done mainly in preparation for supporting a much larger multiplayer
  population. Hearing now queries a separate spatial index of zombies with
  behavior entries. Creation/removal own membership; movement and bulk rebuilds
  publish positions. Loud sounds no longer gather and sort the entire living
  population before discarding zombies without behaviors, while footsteps still
  use local queries. Existing audibility and reaction rules are preserved.
- [x] **Viewer gunshot parity, commit `7640a40`.** Standalone firearm shots now
  emit the same AI noise as multiplayer, independently of audio playback.

The hearing improvement is not proof of end-to-end 100,000-zombie multiplayer
performance. Its synthetic CPU comparison and the existing viewer rendering
benchmarks measure narrower workloads.

## Required work and foreseeable issues

- [ ] **Separate world capacity from per-client budgets.** Multiplayer currently
  permits 1,024 zombies globally and 256 interested zombies per snapshot. Audit
  CLI/scenario validation, ID validation, caches and population-dependent loops
  before raising capacity. IDs are already 32-bit. Keep message/transfer budgets
  bounded; do not blanket-replace unrelated inventory or vehicle limits.
  Start in [protocol.hpp](../multiplayer/protocol.hpp),
  [scene.cpp](../multiplayer/scene.cpp) and
  [server/runtime.cpp](../multiplayer/server/runtime.cpp).
- [ ] **Fix client storage and playback scaling.** Inline corpse caches would
  occupy about 18.3 MiB at 100,000 with current 64-bit layouts, exceeding common
  thread stacks. Move them to heap or sparse storage. Replace population-sized
  temporary lookups and the full population scan in each playback application
  with work bounded by known/interested/changed IDs. Audit repeated applications
  on both snapshot reception and rendering. Start in
  [snapshots.hpp](../multiplayer/client/snapshots.hpp) and
  [snapshots.cpp](../multiplayer/client/snapshots.cpp).
- [ ] **Support persistent state and variable update rates.** Current snapshots
  hide omitted zombies, so simply omitting an unchanged or unscheduled actor
  would make it disappear. Add explicit enter/forget membership, partial state
  updates, per-entity timestamps and stale-update protection. Make membership
  delivery reliable or acknowledged/repeated, including leave/re-entry and late
  joins. Preserve smooth interpolation and complete corpse root/palette pairs.
  Starting network rates could be 20 Hz near, 5 Hz at medium distance and 1 Hz
  far away; tune these through measurement. Network cadence is separate from
  the existing 60 Hz authoritative simulation and client rendering.
- [ ] **Add visibility-aware interest selection.** Aim yaw/pitch alone does not
  describe the actual camera: account for aspect ratio, scope FOV and vehicle
  camera positions. Query spatial candidates, apply a conservative view frustum
  and retention margin, then prioritize within a byte budget. Handle rapid turns,
  zoom changes and approaching threats. Exact wall-occlusion filtering is a
  separate cost to evaluate. A fixed nearest-256 cutoff can hide visible actors
  as local density increases; establish a deliberate density/overflow policy.
- [ ] **Bound corpse and wound replication by interest.** `nextPose()` currently
  scans the entire population cap even when no corpse needs transmission.
  Snapshot selection also appends all world corpse IDs before sorting, and
  cavity contents are sent globally. Use relevant corpse/contents sets instead.
  Preserve versioned poses, acknowledgements, coalescing and bounded in-flight
  transfers. Zombies, wound masks, contents, players, vehicles and props share
  the 256 KiB message budget. Start in
  [server/replication.cpp](../multiplayer/server/replication.cpp) and
  [outgoing_snapshots.cpp](../multiplayer/outgoing_snapshots.cpp).
- [ ] **Avoid unnecessary client population generation.** Clients currently
  construct their own ordinary radial placements before authoritative snapshots
  arrive; the default placement area cannot fit 100,000. Initialize authoritative
  IDs/assets without requiring clients to generate safe positions or routes.
  Preserve stable IDs, asset normalization, fingerprints and late-join behavior.
  Measure startup time, peak RAM, renderer reservations and GPU buffers. With
  current layouts, instance records alone use about 42 MiB per 100,000 actors,
  before indexes, caches and rendering resources; meshes remain shared.
- [ ] **Add explicit idle-population scenario semantics.** Omitting spawn and
  destination bounds currently samples both randomly and preplans every route.
  Introduce an explicit way to generate placements without destinations or
  runtime navigation setup. Preserve existing scenario behavior and ground,
  obstacle, overlap and provenance checks. Start in
  [zombie_scenario.cpp](../src/zombie_scenario.cpp).
- [ ] **Measure remaining shooting and simulation costs.** Accelerated
  `ZombieSpatialIndex::queryRay` traversal was implemented in `d7d38d0`; see the
  [component comparison](../docs/benchmarks/zombie-ray-grid-2026-10-28.txt).
  Continue measuring simultaneous gunfire, exact hit poses and world occlusion
  at the target population/player count. Track the remaining
  [physics streaming, door and wounded-geometry work](suggested-optimizations.md#5-bound-static-physics-streaming-and-measure-construction-hitches).
  Distant pursuers can retain movement toward remembered targets after sight
  suspends, so the active population can grow over time. Several separated
  players, crowded convergence, simultaneous gunfire, awake ragdolls and vehicle
  impacts can exceed the cost of an initially idle population. Visibility must
  not disable authoritative attacks or collision.

## Acceptance and performance evaluation

- [ ] Load a repeatable 100,000-zombie idle population on the published town with
  one client, then up to eight clients. Do not regenerate or resize the town.
- [ ] Cover dense gatherings and accumulated pursuers, combat, vehicles, settled
  and awake corpses, wounds, late joins and repeated movement between regions.
- [ ] Test rapid camera turns/scopes, independently scheduled entity updates,
  packet loss/reordering, constrained bandwidth and slow peers. Verify no stale
  resurrection, missing membership changes, duplicate damage or broken poses.
- [ ] Record startup/peak memory, server tick p50/p95/p99, replication/encoding
  time, client playback/render time, bytes per peer, entity counts and update
  age. Include one and eight players and compare to the current smaller world.
  Agree explicit hardware and CPU/memory/network budgets; target the existing
  60 Hz server tick (16.7 ms) for the representative dispersed workload.
- [ ] Use the [benchmark protocol](../docs/BENCHMARKS.md), fixed simulation steps,
  native GPU checks and controlled comparisons. Retain the standard 100,000
  rendering control, but do not present it or hearing microbenchmarks as full
  multiplayer FPS evidence. On integrated hardware, account for
  [CPU/GPU overlap effects](../docs/benchmarks/cpu-gpu-overlap-2026-10-22.txt).
- [ ] If 100,000 is impractical under the agreed budgets, document the measured
  supported population and limiting workload instead of only increasing caps or
  silently dropping relevant actors.
