Reviewed all **225 C++ source and header files outside `multiplayer/`**, including tests and tools. Found three new issues, plus two already documented defects.

1. **[P2] Repeated building instances share one interior population.**  
   [interior_objects.cpp:79](/home/mateszabo/Documents/projects2026/personal/project-gdll/src/interior_objects.cpp:79) groups buildings by name, ignoring instance identity. Reusing building meshes under different transforms merges their geometry and `populated` flag.

   Reproduced with two buildings 100 m apart: interacting with the first spawned 11 local props and **13 inside the distant building**; interacting with the second spawned nothing. Giving the second instance distinct names fixed both behaviors.

   Use a stable building-instance identifier shared by walls, roofs, and doors. Current generated buildings have unique names, so this affects reused building assets.

2. **[P2] Invalid forest configuration can bypass graceful startup fallback.**  
   [forest_layer.cpp:190](/home/mateszabo/Documents/projects2026/personal/project-gdll/src/forest_layer.cpp:190) can assign one detected tree to multiple overlapping patches. Loading succeeds and publishes generated trees, but [forest_tree_visibility.cpp:13](/home/mateszabo/Documents/projects2026/personal/project-gdll/src/forest_tree_visibility.cpp:13) subsequently throws during renderer construction, outside the automatic forest-loading fallback.

   Reproduced a successful load expanding one tree to 30, followed by renderer-index rejection. Validate exclusive ownership before publishing changes. This requires malformed overlapping patches; valid patches sharing an edge passed a separate check.

3. **[P3] External ragdoll activation leaves an invisible crowd blocker.**  
   [zombie_layer.cpp:208](/home/mateszabo/Documents/projects2026/personal/project-gdll/src/zombie_layer.cpp:208) removes living spatial and behavior membership but omits `crowd_.erase()`. Subsequent corpse removal also leaves that entry behind.

   In a two-zombie reproduction, the pursuer remained blocked after the other zombie became a corpse and was removed. Rebuilding the indexes immediately released it.

   This is a proven API defect, but the current production caller uses external palettes for multiplayer presentation without local AI. I did not find a normal single-player trigger.

Two previously tracked defects remain:

- [animated_model.cpp:502](/home/mateszabo/Documents/projects2026/personal/project-gdll/src/animated_model.cpp:502) overwrites skin-specific inverse binds when skins share joints, allowing baked animation, hit geometry, and ragdolls to disagree.
- [zombie_renderer.cpp:348](/home/mateszabo/Documents/projects2026/personal/project-gdll/src/zombie_renderer.cpp:348) resets animated unskinned primitives to frame zero at ragdoll handoff.

Both remain dormant with the current prepared zombie assets, as recorded in [tasks 7–8](/home/mateszabo/Documents/projects2026/personal/project-gdll/tasks/review-follow-ups.md:60).

**A staged architectural refactor is warranted**, particularly around ownership and identity:

- **Centralize zombie lifecycle changes.** `ZombieLayer` exposes mutable instances while maintaining behavior, crowd, spatial, corpse, hit-geometry, and render state. Owning spawn/move/death/removal operations should update every affected structure. The stale crowd entry demonstrates the maintenance risk.
- **Separate simulation from presentation.** Rendering currently writes displayed-animation fields back into simulation entities. Preserve the useful “shoot the displayed pose” behavior through an explicit presentation snapshot. Keep immutable prepared assets separate.
- **Introduce explicit scene semantics and instance identity.** Multiple systems infer physical meaning and building membership from draw names. Parse that meaning once into shared metadata. Forest loading should similarly produce a fully validated placement plan before mutating the tree layer.
- **Split renderer ownership, not just implementation files.** [Renderer](/home/mateszabo/Documents/projects2026/personal/project-gdll/src/renderer.hpp:182) still owns most GPU resources and layer-specific state. Focused owners for town, trees, forests, and zombies would simplify lifetimes and pass interfaces while retaining centralized render ordering.

The existing movement, collision, spatial-index, and batching modules provide a good foundation. I would preserve those boundaries.

**Verification:** build passed; all **68 viewer tests passed**, including 13 graphics tests on the AMD GPU. Additional viewer and zombie/ragdoll captures were inspected, and the three new findings were reproduced with scratch fixtures. No tracked files changed; `git diff --check` passed.
