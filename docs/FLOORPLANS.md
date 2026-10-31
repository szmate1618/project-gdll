# Runtime family-house floorplans

The standalone viewer, multiplayer client and server generate these interiors
from the map geometry they already load. No town regeneration, sidecar,
serialized floorplan, network geometry payload or random seed is required.
`house_floorplan` classifies the shell and storeys; `house_layout` chooses rooms
and staircase positions; `house_layout_geometry` emits their planes and door
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
ground-storey windows**, including at its endpoints on exterior walls. Pane
bounds come from the already-loaded window geometry, transformed into the
house's axes. Upstairs panes do not constrain ground-storey partitions.
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

## Staircase ramps

A multi-storey house selects one of the living rooms that can accommodate a
staircase, preserving such a room while subdividing. Selection is deterministic
and random among the suitable rooms. Ramps run parallel to the house walls and
prefer placement against an exterior wall. They avoid doorway approaches.

The staircase is currently a **single inclined plane per storey transition**,
1.1 metres wide, with 80 cm landing space at both ends. The slope stays below
the controller's 45-degree walking limit. Multiple flights occupy the same
selected room. There are no treads, railings, upper-storey subdivisions or
furniture rules yet.

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
order. Each building reserves `3 * exterior_door_count + 2` slots: entry and
side-room doors plus the two possible living partitions. Failed random choices
leave unused slots. A reserved slot holds no generated geometry or body until
the house loads. Activation order cannot change IDs. The existing door-angle
snapshot carries interactive movement only; clients ignore angles for reserved
panels whose geometry is not locally resident. Protocol 35 prevents older
peers with different layouts or door-ID semantics from joining.

Walking, ground support, sight, firearm queries and debug geometry include a
small runtime collision BVH alongside the unchanged town BVH. Generation and
eviction invalidate cached door-free/contact/support regions. `PhysicsScene`
replaces the separate runtime Box3D meshes on revision changes while retaining
its already-uploaded town cells. The renderer uploads on topology changes,
updates moving door poses each frame, and draws both camera and shadow passes.

The existing random interior prop population remains a separate system with
its existing session lifecycle. It is not part of the regenerated floorplan.

## Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build -R 'house_layout|floorplan' --output-on-failure
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
