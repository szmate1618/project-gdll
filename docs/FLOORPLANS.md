# Runtime building floorplans

The standalone viewer, multiplayer client and server generate these interiors
from the map geometry they already load. No town regeneration, sidecar,
serialized floorplan, network geometry payload or random seed is required.
`building_shell` reads the shared wall axes, footprint and roof heights.
`house_floorplan` handles family houses; `prefab_floorplan` classifies apartment
blocks and fits their storeys, and `apartment_layout` builds their common areas
and apartment boundaries. `house_polygon` supplies simple polygon clipping,
containment and triangulation; `house_layout` chooses family-house ground
rooms and the first staircase, and `house_upper_layout` generates successive
floors. `house_layout_rules` shares window clearance, room splitting and ramp
fitting between them. `house_layout_geometry` emits their planes and door
panels. `floorplan_layer` owns streaming. Rendering and collision consume the
same generated result separately.

## Eligible houses

This is a footprint heuristic, not a building-use label. A house needs a roof,
at least one prepared exterior door and an approximately orthogonal wall perimeter:

- Oriented bounding-rectangle area between 45 and 220 square metres, with at
  least 45 square metres of actual interior footprint.
- Both bounding-rectangle sides at least 5 metres; neither longer than 22 metres.
- Long side no more than 2.5 times the short side.
- Walls follow two perpendicular axes within **5 degrees**, with **at least
  75% of the bounding rectangle filled**. Modest corner cutouts, entrance recesses
  and extensions can pass. More deeply indented shapes still fail.
- One closed, non-self-intersecting boundary with at most 24 corners after
  removing collinear points. Courtyards, disconnected shells and open/branched
  boundaries fail. Repeated wall-triangle edges are deduplicated within 8 cm.

The rectangle follows transformed wall edges rather than world axes. Node
transforms, including reflections, are applied once. Sheds, apartment blocks
and long narrow buildings usually fail these limits. Small shops
can pass; distinguishing their use needs further rules or additional data.

The highest exterior door heads establish the facade generator's main floor
datum, 2.1 metres below the head. Main doors stop at this datum even downhill.
Grey exterior landings align with the interior walking plane. At rises of
**30 cm or less**, entrances need no ramps: the player, zombies and dogs can
step onto the landing. Masonry closes the wall below these doors. The rise
includes the walking plane's 1 cm offset and is checked over the 1 m wide
doorway approach, including rendered terrain triangle boundaries.
Higher entrances with less than crouching clearance retain their side ramps,
triangular front cheeks, and open underfloor passages where the landing is at
least 40 cm above ground.

Where the 1 m wide approach beneath the landing has at least **1.15 m** of
clearance, the landing becomes a balcony without side ramps. This allows the
player's 1.10 m crouched body plus 3 cm clearance below a **2 cm lintel**.
Three double-sided, alpha-masked textured quads guard its front and sides:
one horizontal top rail with vertical bars underneath, without a lower rail.
The full quads block walking, like mapped fences. A separate interactive door
fills the lower doorway, up to 2.1 m tall. The masonry between the lower and
main leaves keeps them independent when the GLB is loaded. Terrain sampling
uses the rendered triangle boundaries along the doorway and landing approach.

The generated window infills supply distinct row-head heights.
Each additional row adds a full-footprint floor/ceiling plane at that row's
offset from the first row. Adjacent rows must be 2.4–4.2 metres apart. One row,
or no window data, does not invent extra storeys. A final ceiling sits 2 cm
below the lowest roof vertex. Implausibly short/tall shells and inconsistent
head heights are rejected conservatively.

The original rectangular case retains its existing layout choices. Irregular
houses use the same subdivision rules in the two main wall axes, then clip rooms,
partition spans, floors and ceilings to the actual boundary. Rooms must remain
connected and meet the existing size limits after clipping. Optional cuts are
skipped when their rooms or door approaches would extend outside the shell;
stairs and both landings must fit entirely inside it. A recessed exterior door
measures its preferred entry depth from its own facade. Window clearance remains
20 cm on every storey. Geometry never fills a missing corner with the bounding
rectangle or puts a staircase across exterior space.

## Prefab apartment blocks

This separate heuristic applies to larger shells; it does not establish their
actual construction method or use. Classification reads **only the individual
building's wall and roof geometry**, never windows, doors or neighbouring
buildings. It requires:

- A closed, approximately orthogonal footprint, within 5 degrees of its two
  wall axes, without courtyard holes, occupying at least 80% of its envelope.
- Actual footprint area **180–2,200 m²**, and wall height **12–50 metres**.
- Roof relief at most **0.5 metres**.
- Either a slab: short side **8–22 m**, long side **20–120 m**, aspect ratio
  **1.5–8**; or a compact tower: both sides **12–35 m**, aspect ratio at most
  **1.8**. A shell satisfying both uses the slab template.

Height starts at the highest sampled wall base plus the facade generator's
8 cm foundation skin, avoiding a buried downhill wall extending the estimate.
The nominal storey count is rounded from height / 3 m and clamped to **4–16**.
The shared outline limits of 24 corners and one connected perimeter apply.

Generating a usable interior subsequently needs an existing exterior door.
Windows and doors fit the already-classified building: matching window-head
rows can align its floor heights, and partitions must clear windows by **20 cm**.
Repeated upper partitions respect the union of windows on every storey. A
separating floor cannot intersect a pane. Missing panes use evenly spaced
floors from shell heights. A shell can qualify but fail layout fitting; no
partial interior is installed in that case.

A slab has a **2 m common corridor** along its long axis, with apartments on
both sides and a stair core near one end. Deterministic bay lengths target
**6–9 m** and move into window gaps when necessary. A compact tower has a
central landing with up to four surrounding apartment regions. The ground
floor adds lobbies from exterior entrances to the common circulation; upper
floors repeat one common/apartment template. Small remnants merge into an
adjacent apartment or become part of an adjoining lobby. Each retained apartment
has at least **25 m²**, bounding sides of at least **3 m**, exterior-wall contact,
and exactly one **90 cm interactive entrance opening inward** from common space.
Its internal rooms are deferred to a later generation stage. Room categories
are `apartment`, `corridor`, `lobby` and `stair_area`.

The **3.5 × 8 m** stair core contains two alternating 1.1 m ramp bays, with
86 cm end landings and circulation beside the core. Successive flights reverse
direction and use different bays so the next flight and its cutout do not block
the incoming landing. Each transition uses the same walkable inclined-plane
geometry and 2.05 m head-clearance cutout as family houses. The final ceiling
has no stair opening. Common boundaries remain open; apartment partitions use
simple full-height quads and a quad above each doorway.

Rooms tile the actual footprint, including modest recesses or skewed facades.
Clipping keeps small facade fragments until adjacent tiles are joined; common
areas wrapping around the stair core are represented by simple adjoining
polygons, without inserting extra walls. Disconnected areas, unusable door
approaches or an unfittable core reject that candidate layout.

## Ground-storey rooms

Each exterior door independently has a **95% chance** to request a full-width
entry area, preferably **2 metres deep**. Doors on the same facade share an entry polygon when
any request succeeds, with aligned internal doors for the successful requests.
Opposite facade entries remain separate. Strips are carved in stable facade
order so polygons do not overlap. Entry walls search both shallower and deeper
positions, choosing the closest usable candidate to 2 metres and retaining at
least 1.2 metres of entry depth. A request is skipped when no suitable position
can preserve window clearance, usable living space, other entries and the staircase.
A door without an entry area opens directly into its containing living room.

Every created entry independently rolls **50% on each side** for a small room
approximately **2 metres wide**, with the entry area's depth. Side walls can
move to clear windows; each side-room dimension must stay at least 1.5 metres.
A side room is created only when it fits,
leaves a usable anteroom at least 2 metres wide, and clears every exterior door
and aligned internal opening. Both, neither, or either side can be selected.
Their room-category names are **`small_rooms`** and **`anteroom`**; the remaining
rooms are **`living`**. Each small room has an interactive door to its anteroom.

The remaining living space is always processed, even when both entry requests
fail. A deterministic random target chooses **1, 2, or 3 living rooms**. Axis-aligned
rectangle splits create as many of those rooms as fit, with each living room
at least **10 square metres** and **2.5 metres in both directions**. Partition
doorways keep the rooms connected. A split cannot run through another doorway
or consume the space needed by the staircase. Small houses can keep fewer rooms
than the target rather than creating tiny subdivisions.

Every entry, side-room and living partition must stay **at least 20 cm from
windows on its storey**, including at its endpoints on exterior walls. Pane
bounds come from the already-loaded window geometry, transformed into the
house's axes. Panes on other storeys do not constrain a partition.
Candidate positions include clear-gap boundaries between panes, so even narrow
valid gaps can be found; a 1 mm margin protects the clearance after float rounding.
Optional partitions are skipped when no safe, usable subdivision exists.

All choices use a specified integer hash/generator seeded from the stable
building identity and separate domains for entry, side-room, living-room and
staircase decisions. No session RNG, standard-library random distribution,
clock, or activation-order-dependent seed is involved.

Partition walls use one quad for each full-height stretch and one quad above
each doorway. Floors and ceilings are double-sided planes. Ground-storey
planes sit 1 cm above the floor datum to avoid depth fighting with terrain.
Entry doors are up to 1 metre wide; living partitions use 90 cm doors and side
rooms 80 cm doors. All are 2.1 metres high and use the existing interactive
Box3D panels and grab controls. Entry doors swing inward into the living space;
side-room doors swing outward into the anteroom. Panels start closed and retain
3 cm clearance above the floor datum.

## Upper-storey rooms

Each floor above ground starts with its incoming ramp, floor opening and 80 cm
upper landing. Independently for each storey, a deterministic **50/50 choice**
picks a wall direction **D** parallel to either pair of exterior walls. Each
side of the incoming staircase independently rolls **70%** for a full-span wall
parallel to D, preferably about **2 metres from the ramp**. These walls form one
`stair_area` and zero, one or two living areas. A cut is skipped when no usable
side area fits, the storey's windows forbid it, or the incoming opening, landing
or next staircase would be obstructed.

Each created living side independently chooses one of two rules:

- Cut it with walls perpendicular to D into a random **1–4 rooms**, subject to
  the same 10 m² / 2.5 m minimums. Every room gets its own door opening into the
  stair area; the dividing walls need no doorways.
- Reuse the ground-floor living algorithm, targeting **1–3 rooms** connected
  by partition doors and one doorway into the stair area.

Window clearance applies to both modes. A requested subdivision count can be
reduced when necessary. Stair-area boundary doors swing into the stair area.
The uppermost floor gets rooms but no further upward ramp. Room polygons and
door descriptors carry a storey index: 0 is ground, and `upperFloors[i-1]` is
the floor height for storey i. Rendering and collision use that storey's ceiling.

## Staircase ramps

Balcony buildings with lower exterior doors also receive a basement room and
floor when their interiors stream in. Lower doors belong to this room; opening
one can activate the interior just like opening a main door. The main and upper
floor layout choices remain unchanged. If there is a ground-to-upper flight,
a matching flight with the same horizontal footprint and ascent direction sits
directly beneath it, ending at the ground-floor walking plane. Its cutout removes
both rendered and collision floor geometry. Single-storey buildings receive no
invented upward or downward flight. This applies to both family houses and
prefab apartment blocks. The basement uses the lowest lower door's base as its
floor datum. During generation, basement shells extend down to this level and
terrain triangles are clipped only inside their footprints. Exterior terrain
heights and texture mapping remain unchanged. The wall mesh records a local
`building_floor` point to preserve the main-floor datum for shell inspection;
the loader transforms that point once with its mesh.

A multi-storey house selects one of the living rooms that can accommodate a
staircase, preserving such a room while subdividing. Selection is deterministic
and random among the suitable rooms. Ramps run parallel to the house walls and
prefer placement against an exterior wall. They avoid doorway approaches.

The staircase is currently a **single inclined plane per storey transition**,
1.1 metres wide, with 80 cm landing space at both ends. The slope stays below
the controller's 45-degree walking limit. The first flight occupies its chosen
ground room. Subsequent flights are chosen separately
inside each upper stair area, avoiding the incoming floor opening and landing.
There are no treads, railings or upper-storey furniture rules yet.

Each separating floor has a matching **rectangular cutout** over the ramp's
standing head envelope, with 2.05 metres of vertical clearance and a small edge
margin on the sides and lower edge. The upper edge meets the ramp endpoint
exactly so there is no gap before the upper landing. Four simple rectangles
tile the remaining floor. The ramp and cutout
are shared by rendering, capsule walking, sight and Box3D prop collision; an
opening is not merely a visual mask. The final roof-level ceiling stays intact.

Sun shadows already use the original solid building shells, including across
windows and doors. Inside a resident house, exact containment in its oriented
shell keeps interior receivers in shadow, preventing depth bias and filtering
from producing bright strips along roof, wall and floor contacts. A 1 mm margin
handles float32 projection rounding. Ambient and local lights retain their
usual behavior; disabling shadows bypasses this check. Direct sunlight through
windows or open doors would require changing both this check and the sun casters.
For irregular shells this uses the actual polygon, including its recesses. The
rectangle fast path stays constant-cost; irregular shells add at most 24 edge
checks per shaded receiver inside the bounding rectangle while the camera is
inside that house. This does not add a scan of town buildings per pixel.

## Streaming and multiplayer

An entrance grid selects open exterior doors within 20 metres of an observer.
Open means more than 0.1 radians away from closed, avoiding hinge-stop jitter.
The complete building interior is then generated, including its other entries.
Before this trigger, map draw references, exterior panel descriptors and grid
membership are indexed. Apartment-block shell metadata is classified and cached
to reserve stable door IDs; rooms, ramps, partitions and meshes are generated
only on demand. Family-house layout generation remains lazy.

The interior stays resident while an observer is within 30 metres of the
building's oriented footprint and vertical interior interval, even if the
exterior doors close. Standalone and client observers are the local player;
the server considers all connected players. Invalid observer positions are
ignored. No observers means all interiors are evicted. Plans, room polygons,
CPU/GPU meshes, generated collision and interior door bodies are released.
Reentry regenerates the same topology, coordinates, polygons and door IDs.
The server resets generated door movement after a complete eviction, preserving
permanent restraint stretch and release state for the session. A client that
evicts independently recovers the server's current door angles and restraints on return.

Generated interior door IDs are reserved after exterior IDs, in sorted building
order. Each building reserves `3 * exterior_door_count + 2 + 8 * upper_storeys`
slots: ground entry/side-room doors and two possible living partitions, plus at
most four doors per side on each upper floor. Window-head rows determine the
upper-storey count without generating a layout; eligible houses have at most
three upper storeys. Failed random choices
leave unused slots. A reserved slot holds no generated geometry or body until
the house loads. Activation order cannot change IDs. The existing door-angle
snapshot carries interactive movement only; clients ignore angles for reserved
panels whose geometry is not locally resident. A block instead reserves
`storeys * (2 * ceil(long_side / 3 m) + 8) + 6 * exterior_door_count` slots,
a conservative bound for apartment entrances and ground-lobby splits without
generating any rooms at startup. Protocol 39 prevents older peers with different
layouts or door-ID semantics from joining.

Walking, ground support, sight, firearm queries and debug geometry include a
small runtime collision BVH alongside the unchanged town BVH. Generation and
eviction invalidate cached door-free/contact/support regions. `PhysicsScene`
replaces the separate runtime Box3D meshes on revision changes while retaining
its already-uploaded town cells. The renderer uploads on topology changes,
updates moving door poses each frame, and draws both camera and shadow passes.

The existing random interior prop population remains a separate system with
its existing session lifecycle. It is not part of the regenerated floorplan.

## Interior navigation and player house membership

Building footprint metadata is prepared from the map without generating house
layouts. Queries retain concavity and courtyard holes, use transformed world
coordinates, and qualify heights from **10 m below the building's lowest point
through 10 m above its highest point**, including roof/details. A spatial BVH
selects candidates before exact footprint tests. Membership does not require an
open door or a resident interior.

Each authoritative player owns a `PlayerHouseState`: its first sample is
immediate and later samples run every **3 simulation seconds**. `house()`
returns the cached stable building ID, or an empty optional when outside;
`outside()` provides the corresponding boolean. The standalone viewer and
server both publish this cached membership to zombie updates. A borrowed ID in
the update input avoids allocating building-name strings for every zombie.

Every newly resident interior builds a `HouseNavigationGraph` alongside its
geometry. Nodes represent supported room centers, doors, ramp bottoms and ramp
tops. Each center connects to its room's doors and ramp endpoints. Every ramp
endpoint connects to that room's doors and other ramp endpoints on its level,
and each bottom connects to its corresponding top. Apartment common-space
openings use shared portal nodes without a physical panel ID. Same-level edge
paths keep body clearance around concave boundaries, ramps and floor cutouts;
ramp edges follow the actual centerline. Graphs are released with their
interiors and acquire a fresh generation when rebuilt.

An ordinary zombie entering investigation checks the player's cached house and
its own footprint membership. If they share a house, its target belongs there,
and the graph is resident, it enters `houseInvestigating`. A shortest graph route
leads to the room containing the heard sound or partial sighting. It reaches
that room's center rather than pursuing an unseen player's live position.
Existing scene/crowd collisions still constrain every step, and doorway passage
coordination applies while crossing panels. Closed doors remain barriers;
zombies do not operate them. Unreachable graph paths wait rather than cutting
through walls. Missing/evicted graphs fall back to ordinary investigation;
changed target rooms or rebuilt graphs refresh the route. These checks add no
visibility rays or per-frame full-building scans.

## Interior zombie encounters

When a house or apartment block first becomes resident, a deterministic
building-name roll selects it with **20% probability** for an encounter. This is one roll per building,
including all its storeys. Ordinary encounters contain 1–3 zombies, placed
against walls in corners or beside doorways, facing the doorway. Doorway
placements favor the hinge side where the opened panel can hide the zombie.
Positions leave capsule clearance and avoid stairs, unsupported floor openings,
closed panels and existing physical obstacles. Block encounters use apartment
rooms; open boundaries between common-space polygons are not ambush walls.

For 10% of selected encounters, placement first tries a **complete group of 3–6**
in the middle of one living room or apartment. If the whole group cannot fit
the geometry, placement falls back to wall ambushers. At activation, nearby players and living
zombies also block placements; a center group is accepted or rejected as a whole.
Thus 20% is the encounter-selection chance, not a guarantee of occupied houses
when valid positions or capacity are unavailable.

Every activated interior actor immediately receives a behavior entry. On the
ground floor, doorway, beside-door and corner placements start **hiding**:
they turn in place toward audible noises, but never investigate noise or partial
sightings. A full player sighting ends hiding permanently and starts ordinary
chasing. Ground-floor center groups start as ordinary idle zombies.

All actors above the ground floor instead start in **midnight**, regardless of
placement kind. They remain visible and damageable, but do not turn to noise,
observe players, move or attack until the next midnight on the session's game
clock. A surviving damaging hit wakes them early into normal looking toward
the hit's source. Either activation makes them ordinary zombies permanently.
Clock activation uses an absolute simulation-time deadline, so a step that skips
over 00:00 or a house unloaded through midnight cannot miss it. A clock frozen
away from midnight leaves them inactive; spawning at midnight, including a clock frozen there,
activates them immediately. Damage also cancels pending midnight dormancy while
AI is disabled, without executing the disabled behavior. Pending hiding/midnight
policies survive geometry eviction and AI disabling; they do not reset on reentry.
These rules run on the standalone viewer and authoritative server; replicas
receive ordinary actor setup and movement without running a second behavior system.

The standalone viewer and authoritative multiplayer server reserve **10,000
additional zombie slots in one shared pool per session** when zombies are enabled
and the map has doors. This is a fixed addition to the outdoor/scenario population,
not a reservation per house. No house layouts are generated for sizing the pool.
Maps without doors and disabled zombie populations allocate no interior pool.
Multiplayer supports 100,000 requested outdoor/scenario zombies plus this pool,
for up to 110,000 negotiated actor IDs. Replicas load the advertised total once;
they never add another reserve or choose encounter populations themselves.

Slots begin hidden, outside living spatial queries, AI and rendering. They share
character meshes/animations and have their renderer instance capacity allocated
at startup. First encounters consume consecutive IDs from the pool. Activation
changes actor state without growing the population, rebuilding crowd buffers or
resetting existing scenario routes. The server sends active actors through the
existing reliable setup and movement snapshot paths; no floorplans are transmitted.
Clients retire absent movement through the existing five-second interest timeout;
sleeping does not add a new wire message.

Each selected house is attempted once per session. On geometry eviction, living
zombies still inside it sleep with their ID, position and injuries retained; they
wake at those positions when it regenerates. Zombies that have escaped continue
outdoors. Dead actors follow normal corpse cleanup and never respawn. Slots are
not recycled, including after death, so exhausting the pool suppresses new
encounters for the rest of the session. An empty or blocked first attempt is also
remembered. This state is session-local and is not saved to disk.

The reserve has a measured capacity cost of approximately **87 MiB for an embedded
host** (client, navigated server and GPU combined) with all ten installed character
variants. See [the memory estimate](benchmarks/interior-zombie-reserve-2026-10-31.md)
for component costs, measurement conditions and limits. Inactive slots still cost
memory and population-sized bookkeeping; activation adds zombie AI,
physics, injury and replication work.

## Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build -R 'house_.*layout|house_footprint|floorplan|interior_zombies' --output-on-failure
# Optional read-only check against the published map:
./build/test_floorplans output/godollo.glb
# Requires a working OpenGL display:
./build/test_floorplan_renderer build/floorplan.ppm
```

The focused tests cover footprint rejection, rotated/reflected geometry,
window-derived storeys, independent entry/side-room probability, minimum room
sizes, nonoverlapping polygons, connected doorways, inward entry and outward
small-room physical hinges,
20/30-metre hysteresis, multiple server observers, identical regeneration,
cached walking/sight/prop collision eviction, camera/shadow rendering and
independent multiplayer generation with stable replicated door movement. Ramp
tests exercise actual standing-player ascent, transformed sloped support,
upper landing support, multi-storey flights and collision-free floor openings.
Upper-storey cases cover independent side rolls and subdivision modes, connected
room graphs, storey-specific windows, reserved doors, and incoming/outgoing stair
clearance. The GPU case opens an upper door; multiplayer replicates its angle
across independent generation, eviction and reentry.
Interior encounter tests exercise selection frequency, reflected/rotated
placements, upper floors, actual opened-panel concealment, complete middle-room
groups, player clearance, pool exhaustion, injury retention, escape and no
respawn after eviction. Scenario checks retain existing paths while activating
reserve slots. Multiplayer checks deliver reserved actors through setup and
movement snapshots; the GPU regression activates them after CPU animation frames
have been released, then verifies disappearance and reappearance across sleep.
Irregular cases cover corner cutouts, recessed entrances, small extensions and
slightly skew walls, with one to three storeys, rotated/reflected coordinates,
area conservation, walking support, door clearance and exact regeneration. GPU
checks retain exterior depth in a missing corner and verify that its sunlit
receiver is not included in the house's analytic shadow mask.
