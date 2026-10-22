# Potential Surface Steering: hierarchy and pocket experiment

Child branch `experiment-zombie-hierarchical-potential` starts at `543999e`,
the head of `experiment-zombie-potential-surface`. This compares the existing
tangent controller with structural hierarchy bumps, verified pocket guidance,
and their combination, preserving the earlier reachability diagnostics.

**Keep the existing tangent controller as the baseline.** These fixed guidance
settings reduce completion: baseline reaches 1,199 of 2,000 destinations,
hierarchy 801, pockets 1,179, and combined 803. This evaluates one declared
geometry detector and set of field parameters; it does not rule out other
hierarchies, pocket detectors or controller settings.

## Results

Each mode receives the same two 1,000-agent workloads. Stalls and timeouts in
the last two columns are combined across both workloads; all endpoints remain
valid and no request is filtered out.

| Mode | Original arrivals | Second-seed arrivals | Combined arrivals | Combined stalls | Combined timeouts |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline tangent | **599** | **600** | **1,199 / 2,000 (59.95%)** | 561 | 240 |
| Hierarchy | 398 | 403 | 801 / 2,000 (40.05%) | 977 | 222 |
| Pockets | 592 | 587 | 1,179 / 2,000 (58.95%) | 574 | 247 |
| Combined | 403 | 400 | 803 / 2,000 (40.15%) | 972 | 225 |

A rescue is a baseline failure that arrives with guidance; a loss is a baseline
arrival that fails with guidance. Paired changes show why individual successful
examples do not establish an overall improvement.

| Trips | Mode versus baseline | Rescued | Lost | Net arrivals |
| --- | --- | ---: | ---: | ---: |
| Original | Hierarchy | 35 | 236 | −201 |
| Second seed | Hierarchy | 27 | 224 | −197 |
| Original | Pockets | 16 | 23 | −7 |
| Second seed | Pockets | 15 | 28 | −13 |
| Original | Combined | 41 | 237 | −196 |
| Second seed | Combined | 30 | 230 | −200 |

Across both workloads, hierarchy rescues 62 and loses 460 trips; pockets rescue
31 and lose 51; combined rescues 71 and loses 467. Restricting to the frozen
known-reachable strata also favors the baseline: 1,199/1,925 arrivals versus
800 for hierarchy, 1,179 for pockets and 802 for combined. The additional
hierarchy/combined arrival is previously unresolved agent 118, discussed below.

The hierarchy regression is not explained by demonstrated gradient cancellation.
For the original workload, all 211 baseline arrivals that become contact stalls
with hierarchy still have combined descent pointing outward at their final
positions. The minimum combined/fine gradient norm ratio is 95.1%, with maximum
deflection 5.2° (rounded). Altered trajectories, tangent episode history and
contour decisions are plausible mechanisms; these observations do not prove
the cause of each failure. The retained
[subset measurements](../build/potential-surface-hierarchy/20261021-structural-guidance/diagnostics/hierarchy-stalled-subset.json)
and [regression diagnostics](../build/potential-surface-hierarchy/20261021-structural-guidance/diagnostics/hierarchy-regression-diagnostics.json)
keep those measurements separate from the outcome comparison.

## CPU and native buffer memory

These are matched first-100-tick samples, steps 0–99, with all 1,000 agents
active in every mode. Each cell gives mean / p95 process CPU milliseconds per
tick. Guidance changes positions and local queries even within this window;
the figures compare complete update behavior, not isolated equation cost.

| Mode | Original CPU ms/tick, mean / p95 | Second-seed CPU ms/tick, mean / p95 | Native shared guidance buffers |
| --- | ---: | ---: | ---: |
| Baseline tangent | 3.568 / 4.235 | 2.822 / 3.471 | 0 B |
| Hierarchy | 3.176 / 3.986 | 2.422 / 2.924 | 949,820 B |
| Pockets | 3.694 / 4.757 | 2.754 / 4.747 | 75,124 B |
| Combined | 4.246 / 6.133 | 2.980 / 3.347 | 1,024,688 B |

All modes additionally use 24,150,928 B of shared World buffers and 300,000 B
of simulation scratch for the 1,000-agent run. These are native buffer counts,
not process RSS; JSON parsing, transient setup and growing trace/result
containers are outside those totals. The scratch count includes fixed controller
state, current segments and the packed movement-record buffer. The guidance JSON
is 2,476,601 B; its offline bake took 9.097 s, outside simulation timing.

There is one measured pass and no warmup. CPU 2 scheduling and differing
trajectories limit speed conclusions. Smaller whole-trip totals can result from
earlier failures and less active work; losing hundreds of completed trips is
not a useful speed improvement. Exact timings, updates and per-mode outcomes
are retained in the [measurement CSV](potential-surface-hierarchy-measurements.csv).

## Representation and methods

All guidance is stored as equation parameters and sparse polygon geometry.
There is no sampled potential grid, occupancy array, navmesh, per-agent graph
search or stored route. Each agent retains fixed-size local steering and exit
state. The original 147,495 circular tree trunks remain in the local field and
physical collision model; they create no coarse bumps or pocket boundaries.
Tree geometry also checks that a proposed pocket exit target is physically free.

### Hierarchical bumps

A deterministic median-split BVH contains the original physical AABBs of 9,981
buildings and 14,248 fence panels. Two ancestor-free internal-node frontiers
select spans of 16–64 m and 64–192 m. Every selected node contains at least two
obstacles; the world root is excluded. These are spatial BVH groups, not
certified obstacle barriers, and their bounds can include streets or empty land.

Each node stores an ellipse with centre at its AABB centre and radii
`sqrt(2) * (half-extent + outset)`, with outsets 4 / 8 m. The field is
`U = A * (1-q)^3` inside the normalized ellipse (`q < 1`), otherwise zero.
Its analytical gradient joins zero smoothly at the support boundary. Raw
amplitudes 0.75 / 1.5 are divided once by the two present levels. Ellipses from
different nodes can overlap; this does not impose a global amplitude bound.
A goal inside the closed raw AABB suppresses that node's field.

The resulting 5,443 bumps comprise 4,235 small and 1,208 large proxies. Their
potential and gradient add to the existing local field before the unchanged
one-sided filter and tangent controller, using the existing weight.

### Verified pockets

The offline baker unions the exact padded structural OBBs. For each connected
polygon, it subtracts that polygon from its convex hull to propose concave bays,
then subtracts every structural obstacle and clips to the inset map domain.
A candidate must have one straight hull opening, with all remaining boundary
on original obstacles. Closed holes without an exit, multiple mouths, map seams,
blocked mouths and uncertain native-slab contacts are rejected. No geometric
tolerance, buffer, snapping or raster is introduced to force certification.

Fixed geometry thresholds require depth at least 3 m, depth/width at least 0.5,
and mouth width 0.604–128 m. Width is measured in already-padded centre space;
the minimum is a detector threshold, not additional physical padding.
These select 65 pockets from 4,429 candidate bays;
they preserve 3,325 exterior and 87 hole vertices. The combined pocket area is
97,649.60 m². This conservative convex-hull detector covers only a subset of
possible concavities and cannot identify every controller failure.

For a destination outside a pocket, its free region acts as virtual geometry
for steering. Its exterior field is
`U(d) = (1-(d/12)^2)^3 / (1+(d/2)^2)` for boundary distance `0 <= d < 12 m`,
and zero beyond. The interior is constant. Distance and gradients are evaluated
from polygon segments; equal-nearest-boundary loci can change the gradient.
The tangent controller's local lookahead also checks this virtual region.
These probes change steering decisions, never the physical movement guard.

An agent already inside receives a local quadratic attractor at the mouth
midpoint plus 2 m along the outward normal. It keeps that target until outside
and at least 1 m beyond the mouth. The active pocket is omitted from its own
virtual repulsion and probes; switching targets resets its tangent episode.
The exterior field/probes then discourage immediate re-entry. If the original
destination lies inside the pocket, that pocket supplies neither avoidance nor
an exit target. An exit target blocked by physical geometry is rejected.

This is local exit guidance, not a computed global route. Internal obstacles,
trees, combined contours and other controller limitations can still prevent
arrival. Sparse geometry alone does not establish that a controller will escape.

The retained baseline's 4,023,971 trace samples provide a coverage check. Of
726 known-reachable failures, 96 (13.22%) have a sample inside a certified pocket,
and 286 (39.39%) have one inside or within 12 m; only 18 finish inside. Successful
trips also visit these regions: 121 of 1,199 enter and 406 approach within 12 m
(120 / 405 when excluding pockets containing their destination). These are
sampled prior exposures, not predicted guidance activations or causal failure
classifications. Sampling can miss short visits, and new guidance changes the
trajectories. Both rescues and regressions therefore need measurement.

### Combined variant

Both field additions are active, with the same pocket exit policy. No parameters
are retuned for the combined run.

## Workloads and measurement

The comparison preserves both previous 1,000-trip workloads byte for byte,
including known-impossible requests. Each runs baseline, hierarchy, pockets and
combined, sequentially on CPU 2 in a Release build. Weight remains
1448.1546878700494, local length scale 2 m and support 12 m. Speed is 2 m/s,
timestep 0.05 s and arrival radius 0.15 m. The original per-agent time budget,
10 s stall rule and physical padding remain unchanged.

There is one full measured pass per case and no warmup. CPU comparisons use the
same first 100 all-active ticks; totals and nanoseconds per update describe the
different complete trajectories and active lifetimes. Single-pass observations
do not establish confidence intervals or a universal speed ranking. World and
guidance setup, traces, stream I/O, native audits and reporting are excluded
from timed update batches and reported separately where available.

The [reachability cross-check](POTENTIAL_SURFACE_REACHABILITY.md) supplies fixed
strata: original 966 reachable / 30 impossible / four unresolved; second seed
959 reachable / 41 impossible. Those labels are never used for runtime steering
or request filtering. Both workloads were previously investigated; neither is
a new holdout. New guidance settings were declared before these full-map
measurements; local-field settings are retained from the earlier tuning study.

Original-set agent **118**, previously unresolved because its start was outside
the conservative cached mesh, arrives with hierarchy and combined guidance.
Those complete recorded movements pass physical audits, and a separate exact-
goal connection check finds their remaining 0.102 / 0.075 m segments entirely
free. This establishes reachability for 118 and leaves **154, 393 and 680**
unresolved. The comparison keeps the prior strata frozen; the new
[positive evidence](../build/potential-surface-hierarchy/20261021-structural-guidance/new-reachability-evidence.json)
is reported separately.

## Validation and reproduction

Four C++ suites cover the original physics, steering, simulation and new guidance.
New checks include analytical gradient finite differences, compact support,
goal-aware suppression, virtual intersections, allocation-free updates, exact
disabled-mode identity, and full physical U-shaped escape without re-entry under
default and tuned local-field parameters. Seven Python baker tests cover rotated
geometry, uncertain hull caps, holes, multiple/closed mouths, map seams, blocked
tree exits, deterministic BVH frontiers and output provenance.

CLI smoke runs exercise all four modes with independent movement audits. Invalid
scene fingerprints, physical padding and fixed equation settings are rejected.
Full-map runs retain every moved segment for an independent circle/OBB collision
audit. The fresh baseline is checked against all retained per-agent values,
traces and original query counts.

Both baseline cases reproduce those prior values exactly and perform zero
guidance queries. All eight full movement audits pass: **297,881,346 moved
segments**, with no collisions, boundary violations, invalid/nonfinite records,
stream discontinuities or final/count mismatches. Independent final result QA
also passes **33,691 checks with zero errors**. Audit geometry retains closed
contacts and full physical padding; display traces are not used for safety checks.

```bash
cmake -S experiments/potential_surface -B build/potential-surface -DCMAKE_BUILD_TYPE=Release
cmake --build build/potential-surface -j4
ctest --test-dir build/potential-surface --output-on-failure
.venv/bin/python -m unittest discover -s tests -p 'test_potential_surface_guidance.py' -v

.venv/bin/python tools/bake_potential_surface_guidance.py \
    --scene /path/to/original/paths.json --output /path/to/new-guidance.json
.venv/bin/python tools/benchmark_potential_surface_hierarchy.py \
    --binary build/potential-surface/benchmark_potential_surface \
    --recovery-run build/potential-surface-recovery/20261021-shapes-normalization-tangent \
    --guidance /path/to/new-guidance.json --output /path/to/new-run --cpu 2
```

The frozen plan, source snapshots, binary, geometry parameters, complete movement
logs, audits and aerial images remain in ignored build outputs. Images are not
added to Git.

- [Frozen plan and hashes](../build/potential-surface-hierarchy/20261021-structural-guidance/plan.json), [complete comparison](../build/potential-surface-hierarchy/20261021-structural-guidance/comparison.json), [tracked measurements](potential-surface-hierarchy-measurements.csv), and [frozen reachability strata](potential-surface-hierarchy-reachability-strata.csv).
- Aerial comparison sheets: [original trips](../build/potential-surface-hierarchy/20261021-structural-guidance/training-guidance-comparison.png) and [second seed](../build/potential-surface-hierarchy/20261021-structural-guidance/second-seed-guidance-comparison.png).
- Sparse geometry: [map overview](../build/potential-surface-hierarchy/20261021-structural-guidance/guidance-geometry.png), [pocket details](../build/potential-surface-hierarchy/20261021-structural-guidance/pocket-examples.png), [equation/polygon parameters](../build/potential-surface-hierarchy/20261021-structural-guidance/guidance.json), and [prior-trace coverage diagnostics](../build/potential-surface-hierarchy/20261021-structural-guidance/geometry-diagnostics.json).
- Original-trip full movement audits: [baseline](../build/potential-surface-hierarchy/20261021-structural-guidance/training-baseline/audit.json), [hierarchy](../build/potential-surface-hierarchy/20261021-structural-guidance/training-hierarchy/audit.json), [pockets](../build/potential-surface-hierarchy/20261021-structural-guidance/training-pockets/audit.json), and [combined](../build/potential-surface-hierarchy/20261021-structural-guidance/training-combined/audit.json).
- Second-seed full movement audits: [baseline](../build/potential-surface-hierarchy/20261021-structural-guidance/second-seed-baseline/audit.json), [hierarchy](../build/potential-surface-hierarchy/20261021-structural-guidance/second-seed-hierarchy/audit.json), [pockets](../build/potential-surface-hierarchy/20261021-structural-guidance/second-seed-pockets/audit.json), and [combined](../build/potential-surface-hierarchy/20261021-structural-guidance/second-seed-combined/audit.json).

## Ten earlier timeout locations in 3D

The [full-size gallery](../build/potential-surface-hierarchy/20261021-structural-guidance/failure-3d/original-vegetation/index.html)
and [contact sheet](../build/potential-surface-hierarchy/20261021-structural-guidance/failure-3d/original-vegetation/failure-3d-contact-sheet.png)
show ten earlier original-set tangent timeouts. Selection uses
`random.Random(20261021).sample(sorted(timeout_agents, key=id), 10)` from all
122 timeouts, without reachability filtering or visual selection. IDs are
564, 468, 609, 894, 50, 466, 477, 377, 668 and 681. Seven destinations are
confirmed reachable; 466, 477 and 681 are disconnected by buildings/fences.

These are native hardware-rendered views of the original GLB and original
vegetation build: 33,541 detected trees plus 113,954 forest trees, matching the
recorded 147,495 trunks. The first preview used newer vegetation; that preview
is preserved separately and the final gallery uses the historical build.
Terrain height comes from actual terrain triangles. Camera selection checks
visibility to every final position; the marker is a projected annotation, not
a simulated roof or aerial position. Green shows approximately the last 60 s
of movement, with hidden sections dashed; orange marks the final position and
blue shows the goal bearing. Some earlier trail samples can be outside the view.

The sample includes near-goal timeouts at 1.2 m (agent 50) and 3.6 m (agent 894),
as well as much more distant failures. These examples are observations, not a
classification of every timeout as a closed orbit. Selection, exact trace
timestamps, camera matrices, visibility masks, source/library fingerprints,
capture code and image hashes are retained in the gallery's
[manifest](../build/potential-surface-hierarchy/20261021-structural-guidance/failure-3d/original-vegetation/manifest.json).
