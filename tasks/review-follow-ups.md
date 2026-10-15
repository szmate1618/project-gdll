# Remaining correctness tasks

Numbers identify findings from the repository-wide review.

- [ ] **1. Preserve the last good map when companion publication fails.**
  In [generate.py](../src/generate.py), the validated pending GLB replaces the
  published map before `report.json` and `ATTRIBUTION.md` are written. A failed
  companion write leaves a new map alongside old metadata despite generation
  reporting failure. Stage all three outputs before publication and roll back
  already replaced files if publication fails; the existing
  [fence updater](../src/fence_scene_update.py) provides a rollback pattern.
  Acceptance: inject failures in report/attribution staging and replacement,
  verify all previous outputs remain byte-identical, and cover first generation
  where no previous outputs exist. Use synthetic fixtures; do not regenerate
  the user's town.

- [ ] **4. Guarantee road and railway surface clearance between probes.**
  [terrain_surfaces.py](../src/terrain_surfaces.py) checks triangle centroids
  and edge midpoints, which can miss terrain ridges inside a surface triangle.
  A 15 m terrain grid with at most 2 m of relief, 8 m road sampling, and a 2 cm
  offset can still leave a road 8 cm underground. Compute clearance against
  the actual rendered terrain triangles, including interior terrain vertices
  and edge intersections, or split surfaces along those triangles. Preserve
  shared seams and both road/railway callers; inspect
  [terrain_profile.py](../src/terrain_profile.py) for existing grid-crossing logic.
  Acceptance: independently compare surface and terrain heights at intersections
  and interior extrema, rather than repeating the implementation's four probes.
  Include the north-to-south height grid `[[1.5, 1, 1.5], [1.5, 0, 2],
  [1.5, 1.5, 0.5]]` over east/north bounds `(-6, -5, 24, 25)`, with a primary road from
  `(0, 4)` to `(24, 4)`; check clearance at `(2, 2)` as a regression.

- [ ] **5. Refresh every matching companion when applying railway textures.**
  [railway_texture_apply.py](../src/railway_texture_apply.py) changes the map
  hash and refreshes trees, but leaves pole, guardrail, and forest fingerprints
  stale. Their loaders reject the updated map, so layers disappear or multiplayer
  startup fails. Extend validated staging, backups, and rollback to these
  companions; reuse the checked fingerprint refresh in
  [road_scene_update.py](../src/road_scene_update.py) where practical. Preserve
  provenance validation and reject unrelated/stale companions before publishing.
  Acceptance: matching companions retain their content with the new map hash,
  stale companions prevent publication, and injected replacement failures restore
  the map and every affected companion. Include an explicitly supplied forest
  configuration, which may live outside the map directory.
