# Potential Surface Steering: building shape and local recovery

Follow-up to the [parameter tuning study](POTENTIAL_SURFACE_TUNING.md) in the
fourth experimental workspace, on branch `experiment-zombie-potential-surface`.

This experiment separates three changes in order:

1. Use the original oriented building rectangles in the potential field.
2. Restore desired speed after the existing analytical velocity filter.
3. Add a persistent passing side and local potential-contour following.

All variants use the previously selected weight 1448.1546878700494, 2 m length
scale and 12 m field support. Geometry and steering are not retuned against the
benchmark trips. The original circle/filter defaults remain available.

## Controlled comparison

Each variant receives exactly the same 1,000 physically clear start/goal pairs
within each workload. The original seed is 1515146562; the second seed is
1515146563. Both were already used in the tuning study, so the second seed is a
repeated comparison workload, rather than a pristine holdout. The source map,
171,724 obstacles, 2 m/s speed, 0.05 s timestep, 0.15 m arrival radius, per-agent
budgets and 10 s stall rule are unchanged. Trees and fences remain present.
No endpoint snapping, component filtering, NPC-to-NPC avoidance, navmesh,
waypoints or global search is introduced.

| Variant | Building field | Steering |
| --- | --- | --- |
| circles-filtered | Padded enclosing circle | Original filter |
| rectangles-filtered | Padded oriented rectangle | Original filter |
| rectangles-normalized | Padded oriented rectangle | Filter, then speed normalization |
| rectangles-tangent | Padded oriented rectangle | Normalization, plus persistent local recovery |

Rectangle fields use signed Euclidean distance to the same padded OBB used for
physical collision. Outside corners use their nearest-point normal; inside the
rectangle the nearest face supplies the signed-distance gradient, with a fixed
choice at exact ties. The existing potential-versus-clearance profile is retained.
The field BVH indexes conservative support bounds for the selected shape.

Normalization rescales the filtered direction to the desired speed, including
the usual distance-to-goal/timestep cap. The strict movement guard can still
shorten or stop an unsafe move. Normalization alone adds no passing-side choice.

## Persistent local controller

Tangent mode normally uses the normalized filtered velocity. It enters a local
avoidance episode when either the goalward or filtered direction is blocked
within a 1.5 m segment, or goal-distance reduction remains below 10% of desired
speed for 0.5 s. Probes are capped at the remaining goal distance.

At entry, two local tangent probes include a 0.1-times-outward bias. They prefer
a physically clear side, then better goal alignment, with a fixed sign breaking
ties; both probes can be blocked, in which case the movement guard still stops
unsafe motion. The side remains fixed for the episode. The outward direction is the negative
normalized field gradient; a vanished gradient retains the last direction,
with the opposite goal direction as an initial fallback.

The controller follows a local potential contour at desired speed. Its target
is `max(0.02, 0.9 * entry_potential)`; the outward correction is
`clamp((current_potential-target)/target, -0.6, 0.6)`. It releases only after
becoming at least 0.25 m closer to the goal than at entry and finding a clear
1.5 m goalward segment and a clear normalized-movement segment. These constants
were fixed from geometric reasoning before the full-map comparison; no parameter
sweep is performed.

The state stores a side, normal and a few scalar progress measurements per
agent. All geometry probes use the BVH; no obstacle list or route is stored,
and steering performs no allocation after world construction. The controller
can temporarily move away from the goal. This is a heuristic contour follower:
the tangent follows the sum of gradients rather than an identified obstacle,
local clearance does not certify a complete route, combined fields can form
closed contours, and recovery can consume the unchanged time budget.

Physical padding remains a 0.30 m body radius plus 0.002 m skin and an extra
0.05 m for buildings. Each agent retains the budget
`max(120 s, 3 * initial_distance / speed + 60 s)`. A stall still means failing to
displace 0.05 m from the current movement anchor over 10 s.

Every proposed move still passes through the same stop-only collision guard
against original padded OBBs, trunk circles and the inset map boundary. An
untimed strict native check validates every accepted update, and a separate
Python auditor checks every recorded moved segment.

## Completion and route quality

**Persistent tangent steering raises completion to 599/1,000 and 600/1,000**, up
from the previous tuned controller's 108 and 117. This is a substantial gain on
both workloads, but approximately 40% still fail to arrive. The controller
remains an experimental local navigator rather than a reliable replacement for
global pathfinding.

| Trips | Variant | Arrived | Stalled | Timeout |
| --- | --- | ---: | ---: | ---: |
| original | circles-filtered | 108 | 886 | 6 |
| original | rectangles-filtered | 133 | 866 | 1 |
| original | rectangles-normalized | 125 | 867 | 8 |
| original | rectangles-tangent | **599** | 279 | 122 |
| second seed | circles-filtered | 117 | 869 | 14 |
| second seed | rectangles-filtered | 144 | 855 | 1 |
| second seed | rectangles-normalized | 140 | 856 | 4 |
| second seed | rectangles-tangent | **600** | 282 | 118 |

All variants have zero invalid endpoints and invalid movements. Each successive
step is a controlled addition with the preceding variant as its comparator:

| Trips | Change | Both arrive | New successes | Lost successes |
| --- | --- | ---: | ---: | ---: |
| original | rectangle fields | 84 | 49 | 24 |
| second seed | rectangle fields | 94 | 50 | 23 |
| original | normalization | 125 | 0 | 8 |
| second seed | normalization | 140 | 0 | 4 |
| original | tangent recovery | 115 | 484 | 10 |
| second seed | tangent recovery | 134 | 466 | 6 |

Building rectangles improve aggregate completion without universally improving
each trip. Normalization by itself loses arrivals in both workloads. Among trips
that arrive with both rectangle variants, normalization reduces median paired
simulated arrival time by **31.67% / 28.19%**, with median paired travel changes
of **+0.030% / +0.024%**. Preserving the instantaneous filtered direction while
changing step length does not preserve the complete discretized trajectory.

All eight original-trip normalization losses and all four second-seed losses
are **timeouts without a single clipped move**. They finish only
**0.162–0.272 m / 0.185–0.258 m** from the goal, just outside the unchanged
0.15 m arrival radius. These are finishing failures, rather than additional
contact stalls. One plausible mechanism is finite-step behavior: if a step of
length `s` has goalward cosine `c` at distance `d`, then
`next_distance² = d² + s² - 2*d*s*c`. A positive goalward component alone does not
ensure distance reduction when normalized motion is nearly tangential. That
mechanism is an inference; no orbit classification is claimed from endpoints.

For the tangent variants, successful trips have median travel / initial-direct-
distance ratios **1.1372 / 1.1492** and p95 ratios **1.7702 / 1.7300**. These
describe their expanded successful populations; they are not a comparison
against the easier baseline success population. Across successes common to
normalized and tangent modes, median paired travel and arrival changes are
zero in both sets, which also does not imply every common trip is unchanged.

Tangent recovery removes the original restriction against deliberate retreat:
the runs contain **6,346,109 / 6,588,754 accepted updates with negative projection
onto the pre-move goal direction**. This counter is conservative: a perfectly
perpendicular step can increase Euclidean goal distance without being counted.

The remaining tangent stalls have median straight-line goal distances of
**1.861 / 1.839 km**. Timeout medians are **43.70 / 66.21 m**. Stalls and timeouts
are separate outcomes; more movement can mean circling or unsuccessful local
recovery. Global reachability is not classified, so these totals do not prove
that every failed request had a physically possible route.

Independent endpoint diagnostics classify the remaining tangent failures:

| Trips | Building stalls | Fence stalls | Tree stalls | Off-contact timeouts |
| --- | ---: | ---: | ---: | ---: |
| original | 111 | 160 | 8 | 122 |
| second seed | 120 | 149 | 13 | 118 |

Every stalled agent had clipped movement, and every timeout had **zero clipped
moves throughout its trip**. There are no off-contact stalls or failed endpoints
near the inset map boundary. Tangent episodes activate for 937 / 910 agents; 912 / 885 agents
perform accepted retreat updates. The failure mix suggests separate remaining
problems: physical blockage at contacts, and unsuccessful local motion without
contact. It does not establish that every timeout followed a closed orbit.

Near-contact classification uses 10 micrometers only as an endpoint diagnostic,
with exact tree-circle distances and original padded OBB geometry. Independent
GEOS and local-coordinate rectangle distances agree within 3.14e-13 m. This
threshold never reduces collision geometry or permits penetration in an audit.

## CPU and memory

The comparison runs serially on CPU 2 in a Release C++17 build. Each case has
one measured full pass, zero warmups, full movement recording and an independent
audit. Timing figures are exploratory **single-pass observations**, with no
confidence bounds or statistical speed ranking.

Fixed-load samples use the exact same first 100 timestep keys, steps 0–99, with
all 1,000 agents active in all four variants of each workload. The measured
update batch includes local steering, its probes, strict physical movement and
outcome bookkeeping. Parsing, BVH construction, native post-update validation,
traces, stream output, independent auditing and rendering remain outside timers.
There are two wall and two process-CPU probes per timestep, without per-agent
clock probes.

| Trips | Variant | Mean CPU / 1,000-active tick | Tick CPU p95 | Whole-trip update CPU | Whole-trip update elapsed |
| --- | --- | ---: | ---: | ---: | ---: |
| original | circles-filtered | 1.726 ms | 2.262 ms | 47.437 s | 104.941 s |
| original | rectangles-filtered | 1.772 ms | 3.187 ms | 38.454 s | 85.075 s |
| original | rectangles-normalized | 1.578 ms | 1.781 ms | 19.619 s | 44.092 s |
| original | rectangles-tangent | **1.999 ms** | 2.421 ms | 102.057 s | 219.674 s |
| second seed | circles-filtered | 1.615 ms | 2.116 ms | 45.760 s | 100.871 s |
| second seed | rectangles-filtered | 1.595 ms | 2.316 ms | 39.610 s | 86.933 s |
| second seed | rectangles-normalized | 2.157 ms | 3.768 ms | 20.544 s | 45.838 s |
| second seed | rectangles-tangent | **2.140 ms** | 2.691 ms | 108.268 s | 233.780 s |

At 20 Hz, the observed tangent update costs correspond to approximately
**4.00–4.28% of one CPU core**, excluding other game work. Whole-trip CPU includes
different active lifetimes, arrivals and timeouts; an early-stalling controller
can have a smaller total while completing fewer trips. The small difference
between second-seed normalized and tangent tick means does not establish a
speed advantage.

All four variants use **24,150,928 B** of shared world storage and **180,000 B**
of simulation scratch for 1,000 agents with movement logging enabled. The
scratch figure includes fixed per-agent controller state, current segments and
the packed output buffer, but excludes growing trace/result containers. No
path or search pool is introduced. The larger scratch allocation is shared by
all modes in this executable, including inactive tangent state in baseline modes.

## Verification and reproduction

The three standalone C++ suites pass. Regressions cover rotated signed-distance
fields and BVH coverage, exact legacy arithmetic/query counts, normalization
without hidden recovery, persistent side selection, cancellation of gradients,
release into a blocked filtered direction, speed caps and independent swept-
circle safety. Full simulations demonstrate safe tangent bypass of a centered trunk and,
under the selected field parameters, a rotated building, including temporary
retreat and the unchanged individual budget. The six
Python audit regressions also pass; new mode/flag parsing and Python syntax
checks pass.

Both full circle baselines reproduce **every retained agent record and query
count exactly**, allowing only the new zero-valued diagnostic fields. The frozen
sources, binary, original/copy requests, scene and selected parameter file are
hash-checked throughout all eight runs.

There are **189,221,762 native-validated agent updates** across the eight runs.
Independent auditing checks **188,393,332 complete moved segments**, with zero
collisions, boundary violations, nonfinite records, invalid IDs, stream gaps or
final/count mismatches. Physical geometry is not reduced by a tolerance. Display
traces are decimated for images; audits use the full movement streams.

Build the standalone target as documented in the original report, then run:

```bash
.venv/bin/python tools/benchmark_potential_surface_recovery.py \
    --binary build/potential-surface/benchmark_potential_surface \
    --tuning-run build/potential-surface-tuning/20261021T125220Z \
    --output build/potential-surface-recovery/new-run --cpu 2
```

For individual cases, `tools/run_potential_surface_experiment.py` accepts
`--exact-buildings` and `--steering filtered|normalized|tangent` alongside the
existing weight, support and length-scale flags. Defaults retain the original
circle fields and filtered velocity. `tools/compare_potential_surface_recovery.py`
rebuilds the paired comparison and measurement CSV from the eight final folders.

Retained run:
[`build/potential-surface-recovery/20261021-shapes-normalization-tangent`](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/plan.json).

- [Frozen plan and source hashes](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/plan.json), [full comparison](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/comparison.json), and [tracked measurement CSV](potential-surface-recovery-measurements.csv).
- Original-trip aerial images: [circle baseline](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/training-circles-filtered/potential-surface-paths.png), [rectangle fields](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/training-rectangles-filtered/potential-surface-paths.png), [normalization](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/training-rectangles-normalized/potential-surface-paths.png), [tangent recovery](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/training-rectangles-tangent/potential-surface-paths.png).
- Second-seed aerial images: [circle baseline](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/second-seed-circles-filtered/potential-surface-paths.png), [rectangle fields](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/second-seed-rectangles-filtered/potential-surface-paths.png), [normalization](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/second-seed-rectangles-normalized/potential-surface-paths.png), [tangent recovery](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/second-seed-rectangles-tangent/potential-surface-paths.png).
- Tangent independent audit JSON: [original trips](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/training-rectangles-tangent/audit.json), [second seed](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/second-seed-rectangles-tangent/audit.json).
- [Failure-contact diagnostics and provenance](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/contact-diagnostics.json) and their [reproduction script](../build/potential-surface-recovery/20261021-shapes-normalization-tangent/contact-diagnostics.py).

Each case retains its exact request bytes, executable/source snapshot, outcome
JSON, per-step timing CSV, complete movement stream and rendered image. Images,
raw streams, executable snapshots and all build results remain ignored artifacts;
no images are added to Git.
