# Gödöllő palace interior

`layout.json` is the authored reference for OSM `way/107918884`, the connected
palace shell. It replaces the generic exterior doors/windows and supplies a
walkable ground and first floor. Detached buildings, the riding hall, garden
structures and the bunker are outside this shell and are not changed.

## Saved result and continuation

- `layout.json` is the editable source for room groups, levels, stairs and
  exterior opening positions. Change this file to refine the reconstruction.
- `roof.json` supplies the separate photo-derived roof sections, cupolas,
  dormers and chimneys. Roof edits retain the floorplan's eave datum.
- `prepared-floorplan.json` saves the exact prepared interior extracted from
  the published map: two storeys, 155 rooms/circulation spaces, 275 interior
  doorways and two stair ramps. It includes the projection, frame, map fingerprint
  and source-definition fingerprint. The snapshot is a reference for reviewing
  or resuming work; runtime reads the same layout embedded in the GLB.
- [Preview of the published result](preview.png) shows the entrance hall,
  ornamental stair, ceremonial hall, Sisi salon, courtyard and town context.
- [Roof overview](roof-preview.png) shows the photo-derived roof in the town.
- `references/` preserves the five user-supplied museum photographs, so future
  work does not depend on the conversation's attachment directory.

The published shell has 13 exterior door openings and 407 window openings.
The generated town GLB and navigation/placement companions remain in the ignored
`output/` directory. The preparation command below reproduces the castle override
from the committed source against an existing compatible town and terrain.

When zombies are enabled, the first runtime interior load populates the palace
with 200 zombies from the shared session reserve. Placement spans both floors
and replaces candidates blocked by players or existing actors. Survivors and
kills persist across interior streaming; re-entry does not create another 200.
Ground-floor actors start idle and upstairs actors retain the standard midnight
wake policy. See [interior encounters](../../../docs/FLOORPLANS.md#interior-zombie-encounters).
`test_palace_zombies` checks the saved geometry, blocked placement alternatives
and streaming lifecycle; it also accepts a town GLB path for checking the live map.

Validation at this checkpoint: palace Python checks, exact-town walking and room
navigation, stable streamed door IDs, staircase support/head clearance, reflected
node transforms, closed building solids, and GPU captures passed. All seven active
map companions matched the published map fingerprint. The full rebuilt C++ suite
passed 246 of 249 tests; remaining failures were `viewer_physics_boxes` (crate
jumping), `multiplayer_zombie_attacks` (movement fixture), and
`multiplayer_game_menu` (90-second timeout). These failures were not fixed in this
castle change.

## Evidence

- [Published ground-floor evacuation plan](https://www.gkrte.hu/user/szosdok/610/Foldszinti_alaprajz_Kiralyi_Kastely.pdf):
  entrance passage, foyer, paired ornamental stairs, courtyard galleries,
  Sisi salons, exhibition rooms and service-room groups. Room numbers in the
  authored ground-floor names follow this drawing.
- [Fekete J. Csaba, 2019, figure 31, printed p. 63](https://real.mtak.hu/93913/1/096.2019.002.pdf#page=39):
  royal apartment circulation and room functions. This is a historical source,
  rather than an exact survey of the current museum.
- User photographs [1000009119](references/1000009119.jpg),
  [1000009118](references/1000009118.jpg), and
  [1000009050](references/1000009050.jpg): first-floor
  museum-board diagrams showing the ceremonial hall, royal suites, exhibition
  wings and the connected rear wings. These are overlapping first-floor views,
  not evidence for additional full storeys.
  [1000009115](references/1000009115.jpg) shows the massing model;
  [1000009112](references/1000009112.jpg) shows the historical uses of the wings.
- [Palace museum: Elizabeth's rooms](https://kiralyikastely.hu/queen-elizabeths-rooms-garden-and-veranda.html)
  documents the relationship between the first-floor royal suite and three
  ground-floor salons.
- [KÖZTI restoration account](https://www.kozti.hu/munkak/godolloi-grassalkovich-kastely-rekonstrukcio/)
  identifies the connected wings, restored interiors and changes in use.
- The small geographic reference outline and facade anchors are derived from
  OpenStreetMap, © OpenStreetMap contributors, ODbL:
  [source way](https://www.openstreetmap.org/way/107918884).

The PDFs and photographs remain references; no image is needed at runtime.

## Coordinates and approximation

The plan uses metres: X follows the street facade between the two geographic
anchors, and Y points from the street entrance into the gardens. Projection and
town origin always come from `src/config.py`. The actual OSM footprint clips
rooms, walls and slabs; courtyard voids remain open. The outline is deliberately
not replaced with a rectangular building. Tiny skewed facade remnants that
cannot hold an 80 cm body receive floors but are not navigation rooms.

This is an architectural gameplay reconstruction, not a measured conservation
model. Main-body room groups are traced and fitted to the existing footprint;
small service subdivisions, upper-floor boundaries and rear-wing bays are
approximations. The first-floor layout follows the photographed hall/apartment
groups where readable; otherwise it repeats the ground-floor organization.
Rear wings repeat on both storeys. Named rooms indicate the source's function,
not a furnished museum exhibition.

Vertical measurements are inferred: first floor +4.6 m and eaves +9.4 m above
the terrain-derived main-floor datum. Wing ridges and cupola profiles are
specified separately in `roof.json`. The old two-level OSM
shell was only 6 m tall including its roof; the palace override corrects that
height. Both ornamental staircases use 2.2 m wide walkable straight ramps with
matching upper-floor cutouts, rather than a detailed reproduction of each
historic flight. Secret/service spiral stairs, vaults, furnishings, painted
decoration and double-height church/theatre spaces are not modeled. Roof towers
are approximations described below. No unsupported third storey is invented.

Main-body opening positions follow the ground plan's facade bays, with inferred
vertical dimensions and repeated first-floor rows. Rear-wing windows repeat
the visible bay rhythm where individual openings cannot be resolved. Panels
are cut out of the masonry, replacing the original generated doors/windows;
glass retains the viewer's existing solid walking collision. Door approaches
are walkable ramps fitted against the published terrain.

## Approximate roof

The roof is reconstructed from these exterior views:

- [Civertan aerial photograph 1](https://commons.wikimedia.org/wiki/File:Godollocivertanlegifoto1.jpg)
  ([saved reference](references/roof-civertan-aerial-1.jpg)) shows the street
  facade, paired central cupolas, main wing ridges and corner domes.
- [Civertan aerial photograph 2](https://commons.wikimedia.org/wiki/File:Godollocivertanlegifoto2.jpg)
  ([saved reference](references/roof-civertan-aerial-2.jpg)) shows the garden
  side, perpendicular wings and the central roof from the opposite direction.
- [Civertan aerial photograph 4](https://commons.wikimedia.org/wiki/File:Godollocivertanlegifoto4.jpg)
  provides another garden-side view of the roof junctions.
- [The national memorial site's facade photograph](https://nekb.gov.hu/public/tortenelmi-emlekhelyek/godollo/godollo-grassalkovich-kastely)
  resolves the central cupola's curved profile, raised ribs, lantern, finial
  and the small dormers beside it.

The two saved aerial JPEGs are unchanged photographs by **Civertan / Civertan
Grafikai Stúdió**, from Wikimedia Commons. They are used under
[CC BY-SA 2.5](https://creativecommons.org/licenses/by-sa/2.5/); attribution and
source links are retained here. They are visual references rather than textures.
The photographs date from the earlier restoration period; their shapes guide
the model, without reproducing temporary roof weathering or construction works.

Nine independent hipped sections follow the connected OSM wings, with inferred
ridge rises of 4.1–5.2 m. Two rounded rectangular tile cupolas sit over the street
and courtyard pavilions; two smaller circular metal domes sit at the courtyard
corners. Closed lofts approximate their plaster drums, curved caps, raised
meridian ribs, lanterns and finials. Four simple dormers and thirteen chimneys
provide the smaller roof silhouettes. All dimensions are estimated from photos;
sculpture, clock faces, precise pediments, individual tiles and detailed leadwork
remain outside this approximation. Detached buildings retain their existing roofs.

The published roof contains **9,500 triangles**: 3,852 for the pitched surface,
888 for tiled cupolas, 3,996 for metal domes/ribs/lanterns/finials, 716 for plaster
drums/dormers/chimneys, and 48 for dormer panes. The earlier generic roof had
138 triangles, so the increase is 9,362 triangles.

`src/palace_roof.py` generates the roof independently of room planning. A 2 m
clipped surface grid approximates hip junctions and valleys, retaining principal
ridge lines. The roof covers the actual footprint, including its boundary
vertices, and meets the existing eaves. The connected body remains closed with
outward winding after float32 export; the cupolas and furniture are separately
closed, material-batched meshes. No roof geometry fills the courtyards or changes
the saved room/door/stair layout. This is visible roof geometry, not an extra
walkable attic or an added storey.

The palace updater below also applies the roof to an existing town, retaining
the usual map backup and companion-fingerprint validation. The saved floorplan
snapshot and preview are refreshed after publication so future work can compare
the roof and interior together.

Roof validation passed: nine focused palace Python checks, 37 building-generation
checks, the rebuilt authored-floorplan C++ check and exact-town walking/navigation.
The complete town retained 9,981 closed building solids; all seven active prepared
companions matched the new map fingerprint. GPU captures verified the published
roof, and its embedded interior was byte-for-byte equivalent as parsed JSON to
the saved interior before the roof change.

## Preparation and runtime

Full map generation picks up the palace override automatically. To update the
already-generated town, without rebuilding the town or downloading geodata:

```bash
.venv/bin/python tools/apply_palace_floorplan.py --map output/godollo.glb
cmake --build build -j4
```

The updater validates the complete candidate map and unchanged terrain, rebakes
navigation, refreshes only placement companions matching the exact original
map fingerprint, and stages all changes before publication. Recoverable copies
are retained under `output/palace-backups/<original-map-sha256>/`.

`src/palace_floorplan.py` prepares the outline, rooms, partitions, openings and
stairs. `authored_floorplan` version 1 is embedded on the original wall mesh.
`src/authored_floorplan_io.cpp` reads the metadata and applies its node transform
once. `src/authored_floorplan.cpp` builds the normal `HouseFloorplan` result;
the shared layer reserves stable door IDs, streams geometry, and supplies
collision, rendering and room navigation to standalone and multiplayer modes.
All clients therefore use the layout in the selected map, with no extra runtime
file or separate network geometry payload.

Focused checks:

```bash
.venv/bin/python -m unittest discover -s tests -p 'test_palace_floorplan.py' -v
.venv/bin/python -m unittest discover -s tests -p 'test_palace_roof.py' -v
cmake --build build --target test_authored_floorplan -j4
ctest --test-dir build -R '^viewer_authored_floorplan$' --output-on-failure
```
