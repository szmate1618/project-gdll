# Gödöllő police station: fictional gameplay interior

[View the four-floor layout](layout-preview.png) and [in-game captures](preview.png).

This override applies only to OSM `relation/3897212`, at Petőfi Sándor utca 6–10.
It replaces generic doors, windows and interior rules for that building. Its
mapped entrance canopy, `way/1564681267` (`building=roof`), is absorbed into the
override as an open glass roof on two posts. The old generic solid extrusion
blocked the recessed entrance; it is removed without changing nearby buildings. Full
generation reads `layout.json`; the saved town can be updated separately:

```bash
.venv/bin/python tools/apply_police_station_floorplan.py --map output/godollo.glb
cmake --build build -j4
```

The updater stages and validates the map, unchanged terrain, rebuilt navigation
and matching placement companions before publication. Originals are retained
under `output/police_station-backups/<original-map-sha256>/`. Other buildings,
including the authored palace, keep their existing geometry and metadata.

## Design and sources

The interior is invented, not a reconstruction of the working police station.
Ground-floor reception, waiting and reporting rooms face the recessed street
entrance. Duty/staff rooms and a rear dining/kitchen group complete that level.
Upper floors contain investigation offices, administration/training rooms and
management/archives respectively. Connected cross corridors and courtyard
galleries serve all wings. Two stair cores use alternating 1.2 m wide ramp bays
with landings, joining the four mapped storeys. Furniture is not authored here.

- [OpenStreetMap relation](https://www.openstreetmap.org/relation/3897212): the
  exact footprint, five open courtyard rings and four-storey tag, from the local
  2026-10-11 extract. © OpenStreetMap contributors, ODbL 1.0. Geographic rings
  and facade anchors are recorded in the definition. The configured projection
  and origin always come from `src/config.py`.
- [Exterior photograph by Globetrotter19, 2015](https://commons.wikimedia.org/wiki/File:Police_Station._-_Pet%C5%91fi_Street,_G%C3%B6d%C3%B6ll%C5%91.JPG),
  [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), saved unchanged
  as [exterior-2015.jpg](references/exterior-2015.jpg): recessed public entrance,
  flat roofs and repeated narrow windows. Opening sizes, secondary entrances
  and courtyard windows are fitted approximations. The model retains the OSM
  four-storey envelope throughout; the photograph shows lower wings, whose
  individual heights are not surveyed or reproduced.
- [Official 2023 catering tender](https://www.police.hu/sites/default/files/G%C3%B6d%C3%B6ll%C5%91%20Rk.%20p%C3%A1ly%C3%A1zat%202023..pdf):
  confirms that kitchen, dining and buffet premises exist. Their location and
  subdivisions here are invented.

Storeys are at +0, +3.2, +6.4 and +9.6 m, with the roof at +12.8 m. These
vertical dimensions are inferred. The datum is the published terrain under
the building; replacement entrance approaches meet that walking surface.
Windows have 1.1 × 1.65 m openings, fitted between partitions with at least
20 cm clearance. Exterior doors and normal-height interior doors use the
existing interactive panels.

## Runtime and verification

`authored_floorplan` version 2 embeds courtyard rings, an exact simple-polygon
floor tiling, normal-height doors and institutional finishes in the wall mesh.
Version 1 palace maps remain supported. Slabs and ceilings exclude every court;
stair cutouts exclude incoming flights. Rendering, collision, streaming and room
navigation consume the same prepared geometry in standalone and multiplayer.
No station-specific runtime file or network payload is required.

```bash
.venv/bin/python -m unittest discover -s tests -p 'test_police_station_floorplan.py' -v
cmake --build build --target test_police_station_floorplan -j4
ctest --test-dir build -R '^viewer_police_station_floorplan$' --output-on-failure
# Also exercise the actual published terrain and shell:
./build/test_police_station_floorplan output/godollo.glb
```

Checks cover exact courtyard-free floor coverage, closed exported solids,
per-building override scope, complete room connectivity, stair support and head
clearance, walking through the entrance, and stable streamed door IDs.
