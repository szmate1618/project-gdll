# Remaining correctness tasks

Numbers are stable identifiers for review follow-ups.

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

- [ ] **6. [P2] Enforce self-contained multiplayer model assets.**
  [scene.cpp](../multiplayer/scene.cpp) fingerprints selected files, but
  [the model loader](../src/model.cpp) follows external buffer/image references
  inside GLBs. Changing an external `geometry.bin` can move a wall from 5 m to
  10 m while leaving the GLB and multiplayer fingerprint identical. Peers can
  therefore pass the handshake with different collision worlds. Normal generated
  runtime GLBs are self-contained; this was reproduced with a synthetic asset.
  Reject external buffer/image references for every model multiplayer loads,
  including map, tree/forest and zombie models. Embedded binary data and inline
  `data:` URIs remain valid. Keep single-player external-file support; inspect
  [guardrail validation](../src/guardrail_layer.cpp) for an existing strict check.
  Acceptance: synthetic GLBs with external buffers or images are rejected before
  joining, embedded equivalents load, and single-player external assets still
  work. Cover the multiplayer model-loading paths, not just the map extension.

- [ ] **7. [P2] Preserve skin-specific inverse binds for shared joints.**
  [animated_model.cpp](../src/animated_model.cpp) stores one inverse bind matrix
  per skeleton joint, so the last skin overwrites earlier skins' bindings.
  Baked animation uses the correct per-skin matrices, but hit geometry and live
  ragdoll skinning can disagree; a two-skin fixture showed a 2 m displacement.
  **Appears dormant with the current prepared zombie assets and their LoDs:**
  none has conflicting inverse binds for a shared joint, so this does not appear
  to affect current gameplay. Preserve the skin association through loading,
  hit queries, and ragdoll rendering before supporting assets with this layout.
  Acceptance: a synthetic asset with two skins sharing a joint but using
  different bindings keeps baked vertices, hit geometry, and the initial GPU
  ragdoll pose consistent for both skins.

- [ ] **8. [P2] Preserve unskinned animation at ragdoll handoff.**
  [zombie_renderer.cpp](../src/zombie_renderer.cpp) forces ragdolls to frame zero,
  including primitives still using baked animation instead of live skinning.
  An animated unskinned primitive can therefore jump when shot, before physics
  advances; a synthetic animation showed a 0.5 m frame difference.
  **Appears dormant with the current prepared zombie assets and their LoDs:**
  none has unskinned mesh nodes, so this does not appear to affect current
  gameplay. Preserve the captured clip, frame, and interpolation for primitives
  that remain baked, including the multiplayer snapshot handoff.
  Acceptance: GPU captures immediately before and after a shot, without a physics
  step, show no pose jump for mixed skinned/unskinned and fully unskinned assets;
  cover idle and locomotion clips while keeping skinned ragdolls working.
