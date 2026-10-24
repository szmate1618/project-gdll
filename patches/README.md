# Box3D physics extensions

## Deforming melee contact meshes

Normal weighted zombie assets use immutable per-bone meshes in
`src/zombie_bone_collision.hpp/.cpp`. This extension now supports the fallback
for non-skinned animated assets.

`box3d-mesh-refit.patch` also extends the pinned version below. It exports the
input-to-welded vertex mapping from `b3CreateMesh` and adds `b3RefitMesh` to update
stored positions, refit every BVH bound, reclassify edges, and refresh surface
metadata without sorting triangles or rebuilding the hierarchy. Invalid vertices
or newly degenerate triangles reject the update before mutation.

The application owns each mutable mesh exclusively, updates it between physics
steps, and calls `b3Shape_SetMesh` afterwards to refresh broadphase bounds and
discard stale contacts. `src/melee_contact_mesh.hpp/.cpp` retains the welded vertex
map and body/shape ownership. It requests a full rebuild when a welded seam
separates or when dropped/degenerate triangles require new topology. Unchanged
poses retain their collision surfaces and contact caches. No lower-detail mesh
or collider approximation is introduced.

The existing patch helper applies both patches, including on cached sources.
`viewer_melee_contact_mesh` checks deformation, all BVH branches, edge flags,
seam/degeneracy rejection, query bounds, stable body ownership, and cleanup.

## Directional friction

`box3d-directional-friction.patch` extends the pinned Box3D commit
`f555ee42084e0b43cbffa863f40bff8117c08896`. Box3D is MIT licensed; the original
copyright and license are preserved in [box3d-LICENSE.txt](box3d-LICENSE.txt).
Client packaging also continues to include Box3D's upstream `LICENSE`.

The extension adds `b3Shape_SetFrictionAxle(shape, localAxle, sidewaysFriction)`
for convex shapes. The axle is in body-local coordinates. Its cross product
with each contact normal gives the rolling tangent, so the basis follows
steering and camber without rotating as the wheel spins. `SetFriction` still
sets the rolling-direction coefficient. A zero axle disables the extension.

The existing surface-material mixing callback combines both directional
coefficients with the supporting surface, including per-triangle mesh materials.
The scalar mesh/overflow and SIMD convex solvers project friction into an
ellipse with separate rolling and sideways limits. Tangent impulses and warm
starts use that wheel-relative basis. A tire resting on its side uses sideways
grip in both tangents. Shapes without an axle retain the original circular
friction calculation.

CMake runs [the patch helper](../tools/patch_box3d_directional_friction.py) after
populating dependencies. It checks whether the exact patch is already applied,
otherwise checks and applies it. Mismatched or partially modified sources fail
configuration. The patch and helper are configure dependencies; cached/offline
builds use the same source changes as fresh builds.

When updating Box3D or this patch, rebase against the pinned source and verify
clean application and repeated application. Preserve unrelated dependency
edits. `viewer_vehicle_traction` checks measured directional drag with straight
and steered axes, a shared diagonal grip budget, moving ground, mesh material
mixing, scalar overflow contacts, and unchanged isotropic shapes.
