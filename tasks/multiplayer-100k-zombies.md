# TODO: Support multiplayer populations up to 100,000 zombies

**Status:** experimental protocol implementation; full-town performance
evaluation remains open.

## Goal

Support up to 100,000 zombies in the authoritative multiplayer world if practical
on the target hardware. Begin with zombies randomly distributed over safe map
ground, initially idle, without a prescribed spawn region or travel destination.
Reuse the viewer's existing crowd, shared assets, spatial indexes and rendering.
This is a world-population target, not a requirement to simulate 100,000 active
pursuers or transmit/render every zombie for every player at once.

Send state primarily for zombies visible to each client, with a conservative
view margin to avoid pop-in during turns. Nearby moving/attacking zombies need
frequent updates; distant visible zombies can update less often. Idle zombies
use the same selected-tier update schedule, keeping timeout-based client
presence simple. The server must continue authoritative gameplay for relevant
offscreen zombies.

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

- [x] **Separate world capacity from per-client budgets.** The experiment permits
  100,000 authoritative zombies, 10,000 selected/tracked per client and at most
  1,024 motion samples per world message. CLI/scenario and ID validation use the
  world capacity; transport, inventory, vehicle and bone limits stay separate.
  See [protocol.hpp](../multiplayer/protocol.hpp),
  [scene.cpp](../multiplayer/scene.cpp) and
  [server/runtime.cpp](../multiplayer/server/runtime.cpp).
- [x] **Fix client storage and playback scaling.** Per-ID metadata, motion history
  and pose caches live on the heap and allocate payloads lazily. Playback visits
  only the sorted tracked-ID list, including timeout and oldest-entry eviction,
  instead of scanning the full population on every application. See
  [snapshots.hpp](../multiplayer/client/snapshots.hpp) and
  [snapshots.cpp](../multiplayer/client/snapshots.cpp).
- [x] **Support persistent state and variable update rates.** Omitted updates
  preserve client replicas, with five-second presence expiry, reliable setup
  acknowledgements and stale-update protection. Per-actor histories interpolate
  the [compact samples and cadence](#proposed-compact-zombie-format); complete
  physics root/palette pairs remain independent. Network cadence is separate
  from 60 Hz authoritative simulation and client rendering.
- [x] **Add visibility-aware interest selection.** Client camera eye, yaw/pitch,
  aspect ratio and scope FOV drive conservative camera/range selection, with
  retention margins, nearby offscreen threats and sun/flashlight caster volumes.
  Occupied cells are ordered by proximity and actor selection stops at 10,000.
  Dense-view visual quality and exact wall occlusion remain evaluation work.
- [x] **Bound corpse and wound replication by interest.** Pose selection walks
  interested corpse IDs rather than the entire population cap. Reliable setup
  revisions carry wound changes; cavity contents are gathered only for selected
  owners and capped at 256 matrices within the remaining message budget.
  Versioned poses, acknowledgements, coalescing and in-flight bounds are retained.
  All populations still share the 256 KiB world-message budget. See
  [server/replication.cpp](../multiplayer/server/replication.cpp) and
  [outgoing_snapshots.cpp](../multiplayer/outgoing_snapshots.cpp).
- [x] **Avoid unnecessary client population generation.** Replica scenes load
  shared assets and hidden stable-ID slots without safe-ground placement,
  navigation or authoritative spatial-index construction. Stable IDs, complete
  normalization and resource fingerprints remain checked. Startup/peak memory
  and renderer/GPU reservations still require representative measurements.
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

## Proposed compact zombie format

The experimental protocol implementation uses the same compact record for
ordinarily animated living zombies at every update rate, including the nearest
group at 20 Hz. Authoritative simulation precision is unchanged; packing and
quantization apply only when encoding network samples. Clients decode into
float32 before interpolation and rendering. Full-population performance remains
subject to the evaluation below.

| Recurring field | Encoding | Bytes |
| --- | --- | ---: |
| Stable zombie ID | Unsigned 32-bit integer | 4 |
| State flags | Packed animation/pose, cadence and format flags | 1 |
| Feet position X/Y/Z | Three signed 24-bit fixed-point coordinates | 9 |
| Facing yaw | Unsigned 8-bit circular angle | 1 |
| Locomotion animation time | Float32 `walkSeconds` | 4 |
| **Total ordinary record** | | **19** |

Flag bits 0-2 carry walking/running/physics mode; bit 3 selects float32 XYZ;
bits 4-5 identify the 3/12/60-tick cadence. Bits 6-7 are reserved and rejected
when set. The cadence lets playback choose its history delay without guessing
from packet loss.

Position uses the shared map/session runtime frame: X east, Y up, Z south,
in meters, with the existing map origin rather than a moving player origin.
Encode each coordinate in millimeters, round to nearest, and write its three
bytes explicitly in little-endian order. Use float64 intermediates for scaling
and rounding before range-checked integer conversion. Decode with explicit sign
extension.
The signed range is -8,388.608 through +8,388.607 meters per axis, with at most
0.5 mm quantization error before float32 reconstruction. Validate the range;
out-of-range positions require an explicitly tagged float32 position variant
(22 bytes with the other fields above), never clipping or integer wraparound.
The format flags precede coordinates so the decoder can select their width.

Encode yaw as 256 evenly spaced headings: 1.40625 degrees per step and at most
0.703125 degrees rounding error. Use nearest rounding and shortest-path angular
interpolation across the wrap. This representation requires an upright living
root; a full 3D rotation requires a separately identified pose representation.

Establish asset identity, idle phase and the complete fixed model normalization
reliably when an instance enters replication. Preserve scale, origin offset,
reflection and shear; do not assume this normalization is identity. For living
zombies, reconstruct `transform = translation(position) * rotation(yaw) *
normalization`. Retain the float32 locomotion clock to preserve walking/running
timing; idle animation uses the shared clock and its established phase.

Each motion sample is complete given that established metadata, without a
dependency on the last sent motion sample. The common authoritative sample tick
belongs in batch metadata outside the 19-byte record. Omission preserves the
previous state. Five seconds without a newer ordinary sample hides the actor;
reliable setup and physics-pose messages alone never restore presence. The
tracked-ID list is capped at 10,000 and kept in ascending ID order; excess
arrivals evict the oldest entries. Stale samples are rejected, and per-actor
history accommodates its update interval instead of assuming the global 100 ms
playback delay covers every cadence.

Visit occupied 20 m spatial cells nearest first, with deterministic cell ties,
then apply camera/range eligibility tests. Stop at 10,000 selected actors.
Members inside a cell retain bucket order; this deliberately approximates
individual distance ranking. Apply cumulative cutoffs to that selection:

| Distance rank | Maximum group size | Network rate |
| --- | ---: | ---: |
| 1-256 | 256 | 20 Hz |
| 257-1,024 | 768 | 5 Hz |
| 1,025 onward | Remaining interested actors | 1 Hz |

Stagger the slower groups across send passes. Idle actors receive ordinary
updates at the same tier rates. Initial and changed animation/physics modes are
sent promptly; wounds and physics poses use reliable delivery. Visibility,
nearby offscreen threats and dense-crowd overflow remain subject to the interest
and byte-budget requirements above; this schedule does not restrict server
gameplay simulation.

For a budgeting workload where all 10,000 actors are interested and require
recurring updates, the groups contain 256, 768 and 8,976 actors. That is
`256 * 20 + 768 * 5 + 8976 * 1 = 17,936` records per second, or **340,784 bytes/s
(2.726272 Mbit/s) per client** at 19 bytes each. This estimate excludes batch and
transport headers, initial metadata, membership, wounds, ragdoll poses and
float32 position fallbacks. Idle actors are included in this recurring cost.

Loose ragdolls and spring-held melee ragdolls retain the separate complete,
versioned full-3D pose channel. A one-byte yaw cannot describe their independent
limb motion. Protocol 27 uses three-byte joint rotations and sparse millimetre
translation deltas against shared skeleton metadata, with full float32 roots
and a matrix fallback for unsupported changing affine bases. The `CorpsePose`
name and snapshot `dead` flag also cover melee spring ragdolls; the compact flag
therefore denotes physics-pose mode, including those active hit reactions.

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
