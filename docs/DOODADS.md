# Experimental single-player forest doodads

The standalone viewer streams the original [branch, stone and beer-bottle
meshes](../assets/doodads/README.md) around the player when a runtime forest layer
is loaded. Run the normal town viewer, walk into woodland, and select
**Object handling** in hotbar slot **0**. Hold left mouse to drag a nearby doodad; release left to drop it;
movement and mouse motion manipulate it. Tap right mouse to drop, or hold
right and release to throw. Right click also pushes reachable props without
grabbing first. Doodads use the shared grabbable-prop physics.

```bash
./build/godollo_viewer output/godollo.glb --fps
```

`--no-doodads` disables this layer. `--no-trees`, `--no-forests`, or a vegetation
selection without runtime forest polygons also prevents its creation.

## Placement and streaming

The grid uses 16 m world-X/Z cells. A cell-to-forest-patch index is built once
from the original, validated forest footprints, including their holes. Before
creating candidates, a cell must intersect woodland: corner tests, polygon
vertices and boundary crossings cover partial overlap and small polygons wholly
inside a cell. Each candidate then needs its own polygon membership test, so
the nonforest portion of a boundary cell remains clear. Building interiors,
missing terrain and slopes steeper than about 49 degrees are excluded.

A fixed integer hash of cell coordinates, seed and candidate slot sets identity,
offset, type, yaw and scale. There are 64 jittered candidate slots per full cell:
48% branches, 50% stones and 2% bottles in expectation. Negative coordinates use
floor division. Rejected candidates do not change other identities; patch order,
exploration order and the camera's orientation do not affect placement. Bases
sample the published terrain/road triangle surface in the runtime Y-up frame.

Cells intersecting a 48 m horizontal radius load; cells entirely beyond 64 m
unload. Empty cells are also cached within that neighborhood. All three meshes
are shared and GPU instanced, with bounds indexed for camera and shadow culling.
There is no permanent object or rigid body for every piece of forest clutter.

## Moving objects across cells

Only doodads within 3 m horizontally acquire dynamic rigid bodies. These use
the existing shared physics/grab system and simple bounding-box colliders, with
approximate masses of 0.25 kg for branches, 1 kg for stones and 0.35 kg for
bottles, scaled by volume. Their static terrain support is prepared before the
physics step. Bodies outside 6 m retire once they are sleeping and unheld;
held and moving bodies remain alive even when their original cell unloads.

Each moved object keeps its original stable ID. A sparse override stores its
current asset-origin position, rotation, type and scale. The presence of that
ID suppresses the original procedural spawn everywhere, while an index of its
**current** cell restores the moved object at its destination. There are no
independent removal and addition lists to keep synchronized. Destinations may
be in another cell, on a road, indoors or outside every forest polygon.

An override is created or updated only after the asset origin moves more than
5 cm or rotates more than 10 degrees relative to its last recorded pose (the
procedural pose until the first override). Movement is accumulated against that
pose, so many small steps still count once they cross either threshold. Smaller
settling remains visible while loaded but does not create saved state. Larger
automatic movements, such as a bottle tipping over, can still create an override.
The thresholds also apply to handled props; smaller deliberate moves may be
forgotten after unloading. Existing saved overrides remain intact.
Branches and stones align to the sampled terrain normal with varying yaw;
bottles start upright. Physics can rotate them when handled. Colliders
approximate the complete bounds, including the gap
between the branch's two forks.

## Saves

Changed poses are saved every five seconds when needed, and on clean exit, to
the map's sibling `<map-stem>.doodads.json`. Select a separate save with:

```bash
./build/godollo_viewer output/godollo.glb --fps \
  --doodad-save build/experiments/forest.doodads.json
```

The save includes the SHA-256 identities of the map and forest configuration,
generation version, seed and cell size. A mismatch or invalid save is reported
before streaming starts; it is not silently discarded or overwritten. Use a
separate save path when experimenting with different map/forest configurations.
The identity is `map SHA-256:forest configuration SHA-256`, using the complete
file contents. Regenerating either file, even for a road-only update, can make
the existing save incompatible. The error names the mismatched field and shows
its saved and expected values. To keep playing with fresh props, choose an
unused `--doodad-save FILE` path; `--no-doodads` also allows startup without
loading or changing saved props. Keep the original save for its matching world;
do not change its hashes to force it into a different map.
IDs are hexadecimal strings, preserving every bit independently of JSON number
precision. Writing uses a temporary file, validates it, then replaces the last
good save atomically on Linux. Save files are ignored by Git and are valuable
player state. A crash can lose changes since the last autosave.

## Source and checks

- [doodad_placement.hpp/.cpp](../src/doodad_placement.hpp): deterministic candidates and forest overlap.
- [doodad_state.hpp/.cpp](../src/doodad_state.hpp): sparse overrides and save validation.
- [doodad_layer.hpp/.cpp](../src/doodad_layer.hpp): streaming, body promotion and moved poses.
- [doodad_renderer.cpp](../src/doodad_renderer.cpp): shared instanced camera/shadow rendering.
- [Doodads.cmake](../cmake/Doodads.cmake): offline asset building and focused test targets.

```bash
cmake -S . -B build
cmake --build build -j4 --target godollo_viewer test_doodad_placement \
  test_doodad_state test_doodad_layer test_doodad_renderer
ctest --test-dir build -R '^viewer_doodad_' --output-on-failure
```

CPU checks cover partial forest cells, clearings, negative coordinates,
deterministic revisits, invalid saves, held-body eviction and carrying a real
physics prop across multiple cells into nonforest terrain. The graphics check
loads all three authored GLBs and checks their visible surfaces, ground
occlusion and shadow-pass integration. It needs an accessible OpenGL display.
