# Runtime family-house floorplans

The standalone viewer, multiplayer client and server generate these interiors
from the map geometry they already load. No town regeneration, sidecar,
serialized floorplan, network geometry payload or random seed is required.
`house_floorplan` classifies the shell and storeys; `house_layout` chooses ground
rooms and the first staircase, and `house_upper_layout` generates successive
floors. `house_layout_rules` shares window clearance, room splitting and ramp
fitting between them. `house_layout_geometry` emits their planes and door
panels. `floorplan_layer` owns streaming. Rendering and collision consume the
same generated result separately.

## Eligible houses

This is a footprint heuristic, not a building-use label. A house needs a roof,
at least one prepared exterior door and a rectangular wall perimeter:

- Footprint area between 45 and 220 square metres.
- Both sides at least 5 metres; neither side longer than 22 metres.
- Long side no more than 2.5 times the short side.
- Every projected wall segment on the oriented rectangle perimeter, within
  8 cm of export precision. Courtyards, concavities and inward recesses fail.

The rectangle follows transformed wall edges rather than world axes. Node
transforms, including reflections, are applied once. Sheds, apartment blocks
and long narrow buildings usually fail these limits. Small rectangular shops
can pass; distinguishing their use needs further rules or additional data.

Door heads establish the existing facade generator's floor datum, 2.1 metres
below the head. The generated window infills supply distinct row-head heights.
Each additional row adds a full-footprint floor/ceiling plane at that row's
offset from the first row. Adjacent rows must be 2.4–4.2 metres apart. One row,
or no window data, does not invent extra storeys. A final ceiling sits 2 cm
below the lowest roof vertex. Implausibly short/tall shells and inconsistent
head heights are rejected conservatively.

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

## Streaming and multiplayer

An entrance grid selects open exterior doors within 20 metres of an observer.
Open means more than 0.1 radians away from closed, avoiding hinge-stop jitter.
The complete building interior is then generated, including its other entries.
Only map draw references, exterior panel descriptors and grid membership are
indexed before this trigger; classification and generated geometry are lazy.

The interior stays resident while an observer is within 30 metres of the
building's oriented footprint and vertical interior interval, even if the
exterior doors close. Standalone and client observers are the local player;
the server considers all connected players. Invalid observer positions are
ignored. No observers means all interiors are evicted. Plans, room polygons,
CPU/GPU meshes, generated collision and interior door bodies are released.
Reentry regenerates the same topology, coordinates, polygons and door IDs.
The server resets generated door movement after a complete eviction. A client
that evicts independently recovers the server's current door angles on return.

Generated interior door IDs are reserved after exterior IDs, in sorted building
order. Each building reserves `3 * exterior_door_count + 2 + 8 * upper_storeys`
slots: ground entry/side-room doors and two possible living partitions, plus at
most four doors per side on each upper floor. Window-head rows determine the
upper-storey count without generating a layout; eligible houses have at most
three upper storeys. Failed random choices
leave unused slots. A reserved slot holds no generated geometry or body until
the house loads. Activation order cannot change IDs. The existing door-angle
snapshot carries interactive movement only; clients ignore angles for reserved
panels whose geometry is not locally resident. Protocol 37 prevents older
peers with different layouts or door-ID semantics from joining.

Walking, ground support, sight, firearm queries and debug geometry include a
small runtime collision BVH alongside the unchanged town BVH. Generation and
eviction invalidate cached door-free/contact/support regions. `PhysicsScene`
replaces the separate runtime Box3D meshes on revision changes while retaining
its already-uploaded town cells. The renderer uploads on topology changes,
updates moving door poses each frame, and draws both camera and shadow passes.

The existing random interior prop population remains a separate system with
its existing session lifecycle. It is not part of the regenerated floorplan.

## Interior zombie encounters

When a house first becomes resident, a deterministic building-name roll selects
it with **20% probability** for an encounter. This is one roll per building,
including all its storeys. Ordinary encounters contain 1–3 zombies, placed
against walls in corners or beside doorways, facing the doorway. Doorway
placements favor the hinge side where the opened panel can hide the zombie.
Positions leave capsule clearance and avoid stairs, unsupported floor openings,
closed panels and existing physical obstacles.

For 10% of selected encounters, placement first tries a **complete group of 3–6**
in the middle of one living room. If the whole group cannot fit the geometry,
placement falls back to wall ambushers. At activation, nearby players and living
zombies also block placements; a center group is accepted or rejected as a whole.
Thus 20% is the encounter-selection chance, not a guarantee of occupied houses
when valid positions or capacity are unavailable.

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
memory and population-sized bookkeeping; activation adds ordinary zombie AI,
physics, injury and replication work.

## Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build -R 'house_.*layout|floorplan|interior_zombies' --output-on-failure
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
