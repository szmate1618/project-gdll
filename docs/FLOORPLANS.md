# Runtime family-house floorplans

The standalone viewer, multiplayer client and server generate these interiors
from the map geometry they already load. No town regeneration, sidecar,
serialized floorplan, network geometry payload or random seed is required.
`house_floorplan` owns classification and geometry; `floorplan_layer` owns
streaming; rendering and collision consume the generated result separately.

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

Each entrance facade gets a full-width, 2-metre-deep entry polygon. Opposite
entrances produce separate entry areas. Entrances on the same facade share
one entry polygon, with a partition opening aligned to each exterior door.
If adjacent facades have entrances, strips are carved in stable facade order
from the remaining footprint so room polygons do not overlap. The remainder
is a separate living-space polygon. Subdivision of that space is future work.

Partition walls use one quad for each full-height stretch and one quad above
each doorway. Floors and ceilings are double-sided planes. Ground-storey
planes sit 1 cm above the floor datum to avoid depth fighting with terrain. An aligned door
up to 1 metre wide and 2.1 metres high uses the existing interactive Box3D
panel and grab controls, with a **0–110 degree inward swing into the living
space** and 3 cm ground clearance. Doors start closed. Upper levels have full
separating planes; stairs and upper-level subdivisions are not implemented.

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

Generated interior door IDs are reserved after exterior IDs, one slot per
exterior door. A reserved slot holds no generated geometry or body until its
house loads. Activation order cannot change IDs. The existing door-angle
snapshot carries interactive movement only; clients ignore angles for reserved
panels whose geometry is not locally resident. Protocol 32 prevents older
peers with different collision and door-ID semantics from joining.

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
ctest --test-dir build -R 'floorplan' --output-on-failure
# Optional read-only check against the published map:
./build/test_floorplans output/godollo.glb
# Requires a working OpenGL display:
./build/test_floorplan_renderer build/floorplan.ppm
```

The focused tests cover footprint rejection, rotated/reflected geometry,
window-derived storeys, polygon areas, both entries, inward physical hinges,
20/30-metre hysteresis, multiple server observers, identical regeneration,
cached walking/sight/prop collision eviction, camera/shadow rendering and
independent multiplayer generation with stable replicated door movement.
