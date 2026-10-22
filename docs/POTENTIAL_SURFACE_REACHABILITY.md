# Potential Surface Steering: reachability cross-check

Follow-up to the [shape and recovery experiment](POTENTIAL_SURFACE_RECOVERY.md),
using its retained two 1,000-agent workloads and the same original endpoints.
No movement simulations are rerun and no endpoints are snapped.

**Some requests are impossible, but they explain a minority of failures.**
Buildings and fences separate 30 original-set pairs and 41 second-seed pairs,
even with every tree removed. Another 367 and 359 failed pairs have complete
navmesh routes that pass the original tree-aware physical collision checks.
Every one of these routes fits the existing travel budget. Four failed pairs
remain unresolved by the cached navmesh.

| Trips | Failed agents | Unreachable from buildings/fences | Valid physical route found | Unresolved |
| --- | ---: | ---: | ---: | ---: |
| original | 401 | 30 | 367 | 4 |
| second seed | 400 | 41 | 359 | 0 |
| combined | **801** | **71** | **726** | **4** |

Confirmed impossibility explains **8.86%** of failures; **90.64%** have validated
routes, and **0.50%** are unresolved. After removing only confirmed impossible
requests, completion is 599/970 (**61.75%**) and 600/959 (**62.57%**).
This adjustment includes the four unresolved original-set trips in its denominator.

## Separating enclosure from steering failure

| Trips | Steering outcome | Agents | Unreachable | Physical route found | Unresolved |
| --- | --- | ---: | ---: | ---: | ---: |
| original | stalled | 279 | 13 | 264 | 2 |
| original | timeout | 122 | 17 | 103 | 2 |
| second seed | stalled | 282 | 15 | 267 | 0 |
| second seed | timeout | 118 | 26 | 92 | 0 |

Both enclosed starts and enclosed destinations occur. Of the 30 original-set
disconnections, 12 start outside the largest free component and target it; 18
start in it and target another component. In the second set, those counts are
21 and 19, with one pair between two smaller components. These are separated
regions in the declared planar collision model, without doors, jumping or
fence-breaking behavior.

The four unresolved original-set IDs are **118, 154, 393 and 680**. Two have a
start outside the cached tree-aware mesh; two have a goal outside it. Their
endpoints are physically clear and share a tree-free physical component. That
is insufficient to decide tree-aware reachability. They are not silently
snapped or counted as unreachable. Second-seed agent 205 also starts outside
the conservative mesh, but its already-audited arrival establishes reachability.

The later [hierarchy and pocket experiment](POTENTIAL_SURFACE_HIERARCHY.md)
establishes a physical route for agent **118**, using audited movement plus a
clear final connection to its exact goal. The tables here retain their original
reference classifications; **154, 393 and 680** remain unresolved after that
follow-up.

## Were timeouts legitimate long trips?

There are 240 timeouts: **43 confirmed impossible, 195 with physical routes,
and two unresolved**. All 195 route witnesses fit their agents' original time
budgets under the experiment's 2 m/s speed, 0.05 s timestep and instantaneous
turning model.

| Reachable timeout subset | Original | Second seed |
| --- | ---: | ---: |
| Agents with route witnesses | 103 | 92 |
| Median navmesh route length | 3.261 km | 3.509 km |
| Median distance actually walked | 8.521 km | 8.992 km |
| Median paired walked / route length | 2.626× | 2.637× |
| Route witnesses fitting original budget | 103/103 | 92/92 |

The medians above use only timeout agents with validated routes; they differ
from the full-timeout-population medians in the earlier distance analysis.
These routes demonstrate that the 195 trips could finish within the allowance.
Their timeout cannot be attributed to every available route being too long.
This does not classify the other 45 cases or identify the detailed cause of
each inefficient steering trajectory.

The search uses weighted A* with weight 8 and a 50,000-stored-polygon cap.
Its routes are approximate, not guaranteed shortest. A route fitting the budget
is sufficient positive evidence. A route exceeding the budget would not prove
that every possible route is too long. No returned witness exceeds the budget.

## Cached mesh checks and original physical geometry

The tree-aware cache is the original sibling workspace's
`project-gdll-navmesh/build/navmesh/world.navmesh`: **1,005,563 triangles,
346 components**. Its source SHA-256 exactly matches the steering replay:
`886833659e1b1f12488eafb1830240ebb6501c43814dc447ecd8474bf0a90047`.
The tree-free four-vertex cache is
`project-gdll/build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh`
in the main navigation repository: **80,277 polygons, 343 components**. Its
non-tree obstacle records and raster bounds match the steering replay exactly.

Both cached meshes use 0.30 m body radius, 0.002 m clearance, 0.05 m extra for
buildings, and another 0.005 m numerical margin. They represent trunks as
rectangles, while steering uses exact circles. Their outer domain is the full
raster rectangle, whereas steering insets it by 0.302 m. Thus neither cached
mesh alone is an exact reachability oracle for steering.

For positive route evidence, the C++ diagnostic locates the exact original
start and goal and compares component labels. Uncovered endpoints stay
unclassified. Only matching components run search. Each found route preserves
both endpoints, has finite coordinates, and checks every segment against both
navmesh coverage and the original PSS collision model: original padded building
and fence rectangles, circular trunks, and the strict inset map boundary.
No collision tolerance or radius shrink is added.

The tree-aware mesh finds **966 / 958** routes across the complete workloads;
every returned route passes physical validation. It leaves 30 / 40 pairs in
different cached components and 4 / 2 pairs with uncovered endpoints. No search
hits its node cap. The tree-free mesh finds 970 / 959 graph routes, but only
20 / 22 pass validation with actual trees included; the others are not used as
physical reachability witnesses.

Route budget checks conservatively allow a separate final substep for every
waypoint segment:

```text
route_time = sum(ceil(segment_length / (speed * timestep)) * timestep)
```

This accounts for frame rounding and permits the same instantaneous turns as
the prototype. Strictly clear straight segments remain clear when subdivided.

An independent Python audit checks all **1,966 accepted routes / 183,256
segments** from both caches against original circular trunks, padded OBBs and
strict map bounds. It finds no collisions, nonfinite coordinates or changed
endpoints. An independent aggregation also reproduces every failed-trip
classification and all 726 budget checks.

## Independent physical component check

For negative evidence, the Python diagnostic constructs the original physical
building and fence OBBs, expanded along their local axes by 0.302 m plus 0.05 m
for buildings. It adds **no cached numerical margin**, removes every tree, and
subtracts the obstacle union from the correctly inset map rectangle. Separate
polygon interiors are free-space components; zero-width contacts do not join
them. Original endpoints use strict containment, with no snapping or tolerance.

This calculation finds **343 physical tree-free components**. All 4,000
endpoints are located. Different components even after removing trees establish
impossibility due to buildings/fences in this model. Matching components alone
do not establish a route with trees.

All 1,199 tangent arrivals, and every arrival across the earlier three variants,
match components. No valid physical route contradicts a physical disconnection.
Geometry validity, domain containment and area partition checks pass. GEOS uses
double-precision polygon vertices; exact tangencies or extremely narrow gaps
can be sensitive to arithmetic rounding relative to native slab calculations.

## Tools, verification and retained results

The C++ checker is an opt-in standalone diagnostic:

```bash
cmake -S experiments/potential_surface -B build/potential-surface \
    -DPSS_BUILD_REACHABILITY_CHECK=ON
cmake --build build/potential-surface --target check_potential_surface_reachability -j4

build/potential-surface/check_potential_surface_reachability \
    --navmesh /path/to/world.navmesh \
    --summary /path/to/training-rectangles-tangent/simulation/summary.json \
    --summary /path/to/second-seed-rectangles-tangent/simulation/summary.json \
    --output /path/to/new-navmesh-check.json
```

`tools/check_potential_surface_components.py --run-directory RUN --output-directory NEWDIR`
reproduces the physical component calculation and earlier-arrival calibration.
`tools/summarize_potential_surface_reachability.py` joins those results with one
or more `--navmesh-check` files and retains per-agent classifications and route
time checks.

Focused fixtures cover disconnected components, uncovered endpoints, exact
original endpoints, finite and zero-length routes, physical tree rejection,
fingerprint and overwrite rejection, and a connected 50,002-polygon chain whose
search hits the 50,000-node limit without being called unreachable. Python
fixtures cover blocked point contacts, positive-width gaps and strict boundary
containment. No movement benchmark is rerun for this read-only analysis.

Retained results are under
[`build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability`](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/manifest.json).

- [Source and checker provenance](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/manifest.json).
- [Tree-aware navmesh check](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/navmesh-with-trees.json) and [tree-free navmesh check](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/navmesh-without-trees.json).
- [Physical component results](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/physical-components-final/physical-rectangles-connectivity.json).
- [Independent route audit and classification cross-check](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/python-route-audit.json).
- [Joined classifications](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/classified/comparison.json), [complete per-agent CSV](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/reachability/classified/requests.csv), and [tracked measurement summary](potential-surface-reachability-measurements.csv).

Raw mesh geometry, full route coordinates and generated analysis files remain
ignored build artifacts. No images or cache binaries are added to Git.
