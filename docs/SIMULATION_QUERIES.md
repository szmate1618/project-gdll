# Simulation spatial queries and candidate reuse

This is a source inventory and optimization design aid, not a measured ranking
or an implemented frame-wide query cache. Frequencies below describe call sites;
one call can traverse many index nodes and test many candidate objects. Movement
subdivisions and collision retries can issue several calls per actor update.

The standalone loop is in [main.cpp](../src/main.cpp); authoritative multiplayer
simulation is in [server/world.cpp](../multiplayer/server/world.cpp). Both advance
movement and rigid-body simulation at 60 Hz. Standalone frames can contain zero
or several steps; queries and movement events occur per solved step. Server
player input processing can repeat movement within one tick. Client prediction
can replay movement too; an application frame is not a universal query epoch.

## Queries during movement and regular updates

| Query and purpose | Caller and cadence | Existing acceleration or reuse | Useful reuse boundary |
| --- | --- | --- | --- |
| Walking support: surface height, normal and type under feet | `CollisionWorld::walkingGroundAt`; player ground snapping, zombie movement and step attempts; often several times per movement update | Static triangle BVH; scenario routes supply a bounded `WalkingGroundCache`; live boxes checked separately | Reuse static candidate triangles within a covered patch; recompute exact support for current X/Z and height limits. A terrain-grid lookup would only replace the terrain part. |
| Body contacts: capsule overlaps and penetration depths | `CollisionWorld::contacts` / `visitContacts`; zombie movement streams blockers, player/step callers retain contact lists | Static triangle/trunk BVHs; scenario routes cache bounded `ContactCandidates`; door/box poses remain live | Gather conservative static candidates for the whole movement envelope, including all corrections and step lifts; retest exact contacts at every attempted pose. |
| Living zombie neighbors: swept body collision and sliding | `ZombieCrowd::constrainFrom`; each nonzero zombie movement segment, with repeated queries during sliding | Shared 1 m X/Z hash grid, 8 m sector population proof for isolated sweeps; current feet stored by stable zombie ID; bounded neighbor-test budget | Batch candidate discovery or reuse candidates across one actor's substeps, while preserving current positions, coverage, order and budget behavior. |
| Current navmesh polygon | `ZombieNavigation::follow`; each unfinished, valid scenario route update | Immutable navmesh spatial index; destination polygon retained; certified coverage regions retain the current polygon | Reuse the current polygon only while geometric containment remains proven; cross-boundary moves retain a complete fallback. |
| Navmesh segment coverage | `Mesh::segmentClear`; scenario velocity validation, waypoint shortcuts, numerical corner handling and final physical movement guard | Exact same-polygon endpoint tests, certified convex coverage rectangles, plus the last exact validated X/Z step | Reuse an identical step or a segment wholly inside a certified rectangle; leaving coverage uses a full check. Route invalidation clears certificates. |
| Tree trunks near the steering horizon | `CollisionWorld::nearbyTrunks`; scenario follow when a nonzero preferred velocity is available | Separate trunk BVH, bounded padded TrunkCandidates per scenario actor, preallocated output and overflow detection; Detour skips sampling when no obstacles exist | Cached static trunk IDs retain original BVH order and are filtered for current height/range. World changes or movement outside coverage refresh the cache; overflow uses the BVH. |
| Player-proximity zombie activation | `ZombieLayer::updateBehaviorImpl`; per update, per supplied player | Shared 20 m zombie grid, candidate deduplication for multiple players, then exact range checks | Reuse covered grid buckets or a conservative candidate superset. Activation is a player-centered query, not one query issued by every zombie. |
| Player visibility and attack visibility | `sampleZombieSight`, `CollisionWorld::lineOfSight`; scheduled active behavior observations and close-range attack checks | FOV/range rejection; scene and trunk BVHs; live door/box queries. Perception normally uses staggered 0.1/0.2/1 s periods; attack checks use 0.1 s | Existing observation schedules already reuse sight state. Exact visibility cannot be shared solely because two zombies occupy the same cell; rays and occluder poses differ. |
| Door activation near moving objects | `DoorLayer::activateNear`; each fixed step around player/camera, each box, each ragdoll, and each active grenade during preparation | Immutable hinge-cell index and idempotent activation of previously activated doors | Gather hinge-cell candidates once per distinct covered region; retain every original exact sphere test, including differing radii. A cell ID alone does not prove the whole cell is within an activation sphere. |
| Zombies near active door sweeps | `ZombieDoorCollision::prepare`; once per fixed step, one query per active door | Zombie 20 m grid; exact horizontal/vertical filtering; selected stable IDs sorted and deduplicated | Nearby doors may share conservative bucket/candidate gathering. Re-evaluate against the correct zombie-state phase and each door's own sweep. |
| Static scene faces for rigid-body collision meshes | `PhysicsScene::prepare` for each box; `Grenades::prepare` for each active grenade | `PhysicsScene::ensureNear` checks 16 m cells and imports each cell/triangle only once | Already persistent region reuse. Repeated preparation often checks cell membership without traversing scene geometry again; measure these separately. |
| Box candidates for contacts, support, pushing and sight | `BoxLayer::query`, reached through player/zombie contacts and ground/sight queries; player pushing is an explicit action | Box3D `OverlapAABB`, filtered to interior boxes owned by the layer | Candidate IDs may be shared for a covered volume at the same physics state; exact tests need current box poses. Cached results must include newly spawned or explicitly moved boxes. |
| Rigid-body contacts and continuous collision | `PhysicsWorld::step`; fixed 60 Hz steps, potentially several steps per application frame, with Box3D substeps | Box3D's own broad phase/contact machinery | This is a separate engine-owned system; do not duplicate it with an application-wide all-body neighbor cache. |
| Grenade/tree overlap | `Grenades::update` for every active grenade when trunks exist; bounded retries on contact | `CollisionWorld::trunkContact` through the trunk BVH | A conservative local trunk list could serve retries; resolve each corrected sphere position independently. This fallback is overlap-based, not a continuous sweep. |
| Grenade throw clearance | `Grenades::canThrow`; each captured combat weapons update while a grenade is equipped, or per server input evaluation; checked again in `throwFrom` on an accepted throw | Body contact query at the eyes through `CollisionWorld` | Reuse within the same unchanged decision, or defer unnecessary readiness checks if weapon/input semantics permit. This is currently recurring even without a throw; a frame cache must account for the changing eye and obstacle poses. |
| Undergrowth contact along the player's swept path | `UndergrowthEmitter::update`; accumulated path queried at about 0.05 s intervals, or earlier when its path buffer fills; idle/discontinuity checks skip work | Static shrub `VisibilityIndex`; swept bounds, path merging and exact local contact tests | Already batches short motion samples. Reuse candidate shrubs only while the entire accumulated path and body dimensions remain covered. |
| Audible zombie candidates | `SceneAudio::update`, standalone client audio; normally one query every 0.2 s | Zombie grid query at 42 m; current positions/deaths read every frame, audio backend applies audible range | Already amortized at 5 Hz; its cadence and radius are audio semantics, not a collision neighborhood. Preserve its reset behavior when the layer changes. |
| Nearby door sound emitters | `SceneAudio::update`; current door colliders within a 40 m half-extent box each audio update | Door hinge-sweep grid; current panel angle and pose read for slam tracking | Cache conservative door IDs while listener coverage is valid; current angles/poses still need sampling. |

Implementation entry points:

- Shared geometry queries: [collision_world.hpp](../src/collision_world.hpp),
  [collision_world.cpp](../src/collision_world.cpp),
  [door_layer.cpp](../src/door_layer.cpp), [box_layer.cpp](../src/box_layer.cpp).
- Motion: [fps_controller.cpp](../src/fps_controller.cpp),
  [player_stance.cpp](../src/player_stance.cpp),
  [character_step.cpp](../src/character_step.cpp),
  [zombie_movement.cpp](../src/zombie_movement.cpp),
  [zombie_crowd.cpp](../src/zombie_crowd.cpp),
  [zombie_navigation.cpp](../src/zombie_navigation.cpp),
  [zombie_path_follow.cpp](../src/zombie_path_follow.cpp).
- Other recurring callers: [physics_scene.cpp](../src/physics_scene.cpp),
  [physics_world.cpp](../src/physics_world.cpp),
  [zombie_door_collision.cpp](../src/zombie_door_collision.cpp),
  [undergrowth.cpp](../src/undergrowth.cpp),
  [scene_audio.cpp](../src/scene_audio.cpp).

## Queries triggered by events

These can be repeated or expensive, but precomputing them every frame would
create work in frames where the event never occurs.

| Query | Trigger and current implementation | Reuse consideration |
| --- | --- | --- |
| Zombies that can hear a noise | Each `ZombieLayer::makeNoise` event: radius query through the zombie grid, then behavior eligibility and exact attenuation checks | Several noises can share conservative bucket candidates; preserve event order, levels and state changes. Do not merge their effects into one observation. |
| Shot target and world occlusion | Each accepted bullet/pellet: `ZombieSpatialIndex::queryRay`, conservative shooting bounds, then exact pose/hit checks and a world visibility segment | Pellets may share conservative candidates, but directions, hit poses and intervening deaths differ. The current ray query visits occupied zombie cells; it does not already use a ray-walk through cells. |
| Zombie blast victims | Each explosion: `queryLivingNear` then center distance, world line of sight and ragdoll activation | Query once for the blast event. Validate IDs after each mutation; different explosions can change survivors and rigid bodies. |
| Rigid bodies affected by a blast | Each explosion: Box3D AABB overlap, deduplicate bodies, distance and world line of sight | Already event-local candidate gathering. Keep body ownership and collision-category filtering. |
| Box/door selection for grabbing | Interaction press: nearest box/door rays and world line of sight | `PlayerInteraction::press` selects with a ray, then the selected interaction handler repeats its ray. Passing the selected hit through the same event could remove duplication without a frame cache. |
| Interior prop support and collision-region import | First interior population following door interaction; terrain support samples and `PhysicsScene::ensureNear` | Setup for that interior, not recurring per-zombie movement. Preserve publication of new boxes to later queries. |
| Spawn/placement checks | Player spawn or scenario/population loading: ground, clearance, building containment and navmesh endpoint checks | Cold setup. Exclude it from steady-state query rates and simulation timing. |

Sources: [zombie_behavior.cpp](../src/zombie_behavior.cpp),
[zombie_shooting.cpp](../src/zombie_shooting.cpp),
[zombie_layer.cpp](../src/zombie_layer.cpp), [grenades.cpp](../src/grenades.cpp),
[player_interaction.cpp](../src/player_interaction.cpp),
[object_interaction.cpp](../src/object_interaction.cpp),
[door_interaction.cpp](../src/door_interaction.cpp),
[interior_objects.cpp](../src/interior_objects.cpp).

Camera, sun-shadow, flashlight and vegetation LoD visibility queries are rendering
queries, outside this simulation inventory. Their result sets do not define
collision or AI membership. Ragdoll pose synchronization and grid/bounds updates
are maintenance work, not spatial searches; record their costs separately.

## Precomputing zombie neighbor candidates

An array indexed by stable zombie ID is a useful possible interface. A compact
layout avoids allocating one vector per actor:

```text
offsets[id] .. offsets[id + 1]  -> range in neighborIds
neighborIds                   -> flat array of candidate zombie IDs
```

This is a proposal. The current crowd already has one shared spatial grid and
an array of current body state by ID. `beginStep()` resets counters; it does not
rebuild per-zombie lists. Only moving actors query neighborhoods, and idle living
actors remain collision obstacles. Corpse/removed actors are excluded.

Moving the same one-off neighborhood query to the start of a step and storing
its answer adds memory traffic without inherently reducing discovery work.
Savings need a concrete source: visiting cell pairs together, discovering an
unordered pair once and publishing both directed entries, or reusing a candidate
list across several subsegments/slides or consumers. Test sparse and dense
populations separately; eager lists for stationary actors can be wasted work.

Construction must use the spatial grid, not all N-squared actor pairs. With
bounded search regions, expected storage/work scales with actor count plus the
number of emitted candidate references. Dense or overly padded regions can
still produce quadratic pair counts. As an illustration, 100,000 actors with
16 directed neighbor IDs each need about 6.4 MB for 32-bit IDs plus 0.4 MB for
32-bit offsets. An average of 64 neighbors needs about 26 MB total. These are
examples, not safe hard limits; check allocation/index overflow explicitly.

Cache candidates, not final collision answers:

1. Build after the relevant physics synchronization and before the intended
   zombie movement phase, rather than assuming the start of a display frame is
   the correct state. The standalone and server update order differs.
2. Cover both actors' maximum displacement for the whole reuse interval, body
   radii, skin and collision-adjusted paths. A current-radius snapshot can miss
   actors that move into collision later. Use a proven bound or refresh/fallback;
   do not rely on average speed or an unbounded teleport fitting a margin.
3. During resolution, read neighbors' current committed feet from the body array
   and repeat exact swept/vertical checks. Existing behavior is sequential: later
   moves see earlier committed moves. Resolving everything against old snapshot
   positions would change collision semantics and can create overlaps.
4. Preserve actor processing order, tie decisions and neighbor-budget behavior.
   `neighborBudget == 64` bounds attempted tests across one movement's segments;
   it is not a guarantee that only 64 potentially colliding bodies exist. The
   current exhausted-budget behavior stops safely. A truncated list must never
   be treated as complete. Overflow needs a complete fallback or a safe stop.
5. Invalidate or refresh for spawns, removals, deaths, teleports, population/world
   replacement, changed body dimensions or any movement beyond the covered
   envelope. Include stationary obstacles even if only movers receive lists.

Pairs are symmetric for candidate discovery; movement decisions are not. One
pair test or one stored collision outcome cannot replace both actors' different
directed sweeps against the evolving scene state.

## Other opportunities and measurement

Static geometry/trunk candidates within a covered region can outlive a frame;
there is no need to rebuild them solely because the display frame changed.
Body contacts are especially interesting because resolution revisits nearby
geometry at several poses. A candidate set must cover every queried capsule,
including stance changes, ground corrections and step attempts, or fall back.
Keep moving door/box poses and newly created objects separate from immutable
geometry caches. A cached height or visibility boolean generally has a shorter
validity than the candidate objects used to calculate it.

Door activation regions and active-door zombie queries can overlap. Deduplicating
covered bucket discovery is worth measuring, while retaining each distinct
range/height test. Audio and undergrowth already amortize queries; their existing
cadences should not be replaced with eager per-frame queries without evidence.

There is also simple call duplication to investigate before adding cache storage:
`updateBehaviorImpl` queries the default player's activation region, then queries
every supplied player when the multiplayer player list is present. That includes
the first player again; sorting/deduplication removes duplicate IDs only after
both searches. Interaction selection similarly repeats the chosen box/door ray
inside its handler. Preserve event and activation behavior when removing either
duplication. Neither requires a population-wide precomputation pass.

For each proposed cache record discovery calls, index-node/bucket visits, emitted
candidates, exact tests, cache builds/reuses/fallbacks, memory and total update
time. Distinguish no candidates from no query: sparse crowds can spend time in
empty cell lookups without incrementing `neighborChecks`. Measure cache building
and reading together, and keep successful output independent of the cache path.

Check candidate completeness and movement against an independent reference for
sparse/dense crowds, cell crossings, large timesteps, slides, existing overlaps,
multi-floor separation, blocked doors, stationary actors, deaths and teleports.
Compare event results, stable collision order and safe budget exhaustion as well
as final positions. Include moving doors/boxes and server/prediction ordering
when sharing candidates beyond one subsystem.

Start with the focused query/crowd/movement tests under `tests/`. Use
[BENCHMARKS.md](BENCHMARKS.md) for performance measurement and retain the
[CPU/GPU overlap caveat](benchmarks/cpu-gpu-overlap-2026-10-22.txt): a CPU-side
change can alter GPU elapsed time indirectly on the tested integrated hardware.
This inventory adds no query batching or simulation behavior changes.
