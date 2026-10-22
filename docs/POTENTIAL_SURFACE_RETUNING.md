# Potential Surface Steering: field retuning

This experiment retunes the obstacle-field weight, length scale and support
for the existing exact-building, normalized tangent controller. It follows the
[shape and recovery comparison](POTENTIAL_SURFACE_RECOVERY.md) and the
[hierarchy and pocket experiment](POTENTIAL_SURFACE_HIERARCHY.md). Structural
guidance is disabled throughout. **Use `weight = 512`, `length = 1 m`,
`support = 6 m` as the baseline for subsequent standalone potential-surface
experiments.** Original-workload arrivals improve from 599 to 857, the reused
second seed from 600 to 855, and the fresh holdout from 607 to 842, each out of
1,000. The fresh holdout still has 158 failures; this remains an experimental
controller.

## Final results

Each cohort receives a fresh baseline run and a fresh selected-parameter run.
The original and second-seed requests were studied previously; only the fresh
holdout was generated after the configuration lock. All six runs retain the
same physical rules and all 1,000 requests, including disconnected trips.

| Cohort | Seed | Parameters | Arrived / 1,000 | Stalled | Timed out |
| --- | ---: | --- | ---: | ---: | ---: |
| Original | 1515146562 | Inherited baseline | 599 | 279 | 122 |
| Original | 1515146562 | **Selected** | **857** | 4 | 139 |
| Reused second seed | 1515146563 | Inherited baseline | 600 | 282 | 118 |
| Reused second seed | 1515146563 | **Selected** | **855** | 7 | 138 |
| Fresh holdout | 1515146564 | Inherited baseline | 607 | 272 | 121 |
| Fresh holdout | 1515146564 | **Selected** | **842** | 8 | 150 |

Across the three cohorts, arrivals rise from 1,806/3,000 to 2,554/3,000
(60.20% to 85.13%). Stalls fall from 833 to 19; timeouts rise from 361 to 427.
Timeout counts alone do not establish worse orbiting: the populations reaching
each terminal state change. The retained paired outcomes distinguish rescues
from lost successes.

| Cohort | Arrive in both | Rescued failures | Lost successes | Neither arrives | Net arrivals |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 590 | 267 | 9 | 134 | +258 |
| Reused second seed | 581 | 274 | 19 | 126 | +255 |
| Fresh holdout | 585 | 257 | 22 | 136 | +235 |

The selection rescues 798 trips and loses 50 previously successful trips, for
748 additional arrivals overall. Among only the same trips that arrive in
both variants, median paired travel and simulated arrival time improve modestly:

| Cohort | Common successful trips | Median travel change | Median arrival-time change |
| --- | ---: | ---: | ---: |
| Original | 590 | −1.97% | −1.97% |
| Reused second seed | 581 | −1.87% | −1.87% |
| Fresh holdout | 585 | −2.19% | −2.19% |

These are paired changes on common successes; they do not measure shortest-path
quality for the newly rescued trips.

![Six final outcomes](../build/potential-surface-retuning/20261022-current-tangent/final-outcomes.png)

## Remaining failures

Independent contact diagnostics use the original padded physical geometry.
Every stalled agent has clipped movement; every timed-out agent has zero
clipped moves throughout its trip and is off contact at its final position.
There are no invalid endpoints, off-contact stalls or map-boundary contacts.

| Cohort | Parameters | Building stalls | Tree stalls | Fence stalls | Off-contact timeouts |
| --- | --- | ---: | ---: | ---: | ---: |
| Original | Inherited baseline | 111 | 8 | 160 | 122 |
| Original | Selected | 0 | 0 | 4 | 139 |
| Reused second seed | Inherited baseline | 120 | 13 | 149 | 118 |
| Reused second seed | Selected | 0 | 0 | 7 | 138 |
| Fresh holdout | Inherited baseline | 130 | 14 | 128 | 121 |
| Fresh holdout | Selected | 1 | 0 | 7 | 150 |

For the selected parameters, timed-out agents have median remaining straight-line
goal distances of 189.7 m, 158.7 m and 791.0 m in the original, second-seed and
fresh-holdout cohorts respectively. These are different failure populations.
This retuning does not establish which fresh-holdout failures are globally
reachable or prove that every timeout follows a closed orbit.

## CPU and native buffer memory

Matched timing uses the same first 100 steps with all 1,000 agents active in
both variants. Each case has one measured pass and zero warmups. Total CPU
sums all timed update batches over the full simulation; its active lifetimes
and trajectory-dependent work differ across variants.

| Cohort | Parameters | Mean CPU ms/tick | p95 CPU ms/tick | Whole update CPU, s | Agent updates |
| --- | --- | ---: | ---: | ---: | ---: |
| Original | Inherited baseline | 3.118 | 4.494 | 103.013 | 40,044,191 |
| Original | Selected | 2.475 | 3.122 | 121.463 | 49,825,253 |
| Reused second seed | Inherited baseline | 3.579 | 4.595 | 105.417 | 40,487,422 |
| Reused second seed | Selected | 2.212 | 2.784 | 122.514 | 50,367,338 |
| Fresh holdout | Inherited baseline | 2.656 | 3.096 | 109.692 | 38,839,356 |
| Fresh holdout | Selected | 2.426 | 3.369 | 118.603 | 48,978,268 |

Observed matched-window means decrease in all three cohorts, while the fresh
holdout p95 increases. Whole-simulation CPU and update counts increase in every
cohort. These single-pass measurements do not establish a statistical speed
ranking or isolate the cost of an equation from the changed trajectories.
The [measurement CSV](potential-surface-retuning-measurements.csv) retains exact
timings, updates and all field/collision query counts.

Native buffer sizes are identical in every final case: 24,150,928 B for the
shared World and 300,000 B of simulation scratch for 1,000 agents, with zero
guidance buffers. Scratch includes the 36,000 B preallocated movement-record
encoding buffer. These figures exclude JSON parsing, transient setup, result
and trace storage, stream-library and process overhead; they are not RSS.

![Final CPU observations](../build/potential-surface-retuning/20261022-current-tangent/final-cpu.png)

## Parameter-selection results

The completed search contains 72 trials: one full-workload compatibility check,
56 coarse and 12 refinement trials on the same 128 requests, and three
full-workload finalists. The inherited full-workload baseline remains eligible.
The following table reports selection runs on the original 1,000 trips; these
are separate from the subsequent six freshly audited final cases.

| Configuration | Weight | Length, m | Support, m | Arrived | Stalled | Timed out | Mean clipped remaining / initial |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **Locked selection** | **512** | **1** | **6** | **857** | 4 | 139 | **0.045984** |
| Other finalist | 128 | 1 | 3 | 857 | 1 | 142 | 0.056804 |
| Other finalist | 64 | 1 | 3 | 854 | 1 | 145 | 0.070340 |
| Inherited baseline | 1448.1546878700494 | 2 | 12 | 599 | 279 | 122 | 0.188503 |

Two finalists tie at 857 arrivals. The declared secondary criterion chooses
weight 512, length 1 m and support 6 m because its mean
`min(1, remaining / initial)` is lower. This choice does not use CPU time or
prefer fewer stalls over timeouts. The winner was locked before the new holdout
was generated.

At inherited weight and length, changing support alone gives the following
subset results. Each denominator is 128, and these numbers do not establish
full-workload or holdout performance.

| Support, m | Arrived / 128 | Stalled | Timed out |
| --- | ---: | ---: | ---: |
| 1.5 | 105 | 0 | 23 |
| 3 | 102 | 0 | 26 |
| 6 | 83 | 17 | 28 |
| 12, inherited | 77 | 37 | 14 |
| 24 | 64 | 52 | 12 |
| 48 | 62 | 57 | 9 |

The best support-only subset result is 1.5 m at the inherited weight and
length. The joint search selects 6 m with a different weight and length;
support does not have one measured optimum independent of those parameters.
This finite search does not establish a global optimum.

The selection halves both length and support, preserving `support / length = 6`.
For one obstacle at field clearance `c`, its unweighted potential profile
therefore satisfies `U_new(c) = U_old(2c)`. Raw weight decreases, but the
single-obstacle boundary filter coefficient `weight / length²` increases from
362.0387 to 512, about 41.4%. This is a more localized field with stronger
boundary filtering despite the lower raw weight. Reduced field overlap
is a plausible contributor to improved training completion; this comparison
does not prove the mechanism. Support also enters the smooth taper inside the
field, so changing it affects nearby gradients as well as the maximum query
range.

![Support-only subset comparison](../build/potential-surface-retuning/20261022-current-tangent/support-screening.png)

The retained [plan](../build/potential-surface-retuning/20261022-current-tangent/plan.json),
[trial results](../build/potential-surface-retuning/20261022-current-tangent/results.json),
[winner lock](../build/potential-surface-retuning/20261022-current-tangent/winner.json)
and [tracked trial CSV](potential-surface-retuning-screening.csv) preserve the
selection evidence.

## Fixed behavior

The inherited baseline is `weight = 1448.1546878700494`, `length = 2 m`,
`support = 12 m`. Buildings use their padded oriented rectangles for the field,
trees use circles and fences retain the existing capsule field. The persistent
tangent controller, its activation and release thresholds, local probes and
per-agent state remain unchanged. Only the three field parameters vary.

The original scene retains all 171,724 obstacles, including trees. Physical
movement uses the same strict circle and oriented-rectangle guards: 0.30 m
agent radius, 0.002 m skin and an additional 0.05 m building allowance. Closed
contact is forbidden. Speed remains 2 m/s, timestep 0.05 s and arrival radius
0.15 m. A stall still means insufficient displacement under the existing
10-second, 0.05 m anchor rule; the individual simulation budget remains
`max(120 s, 3 * initial_distance / speed + 60 s)`.

These are independent agents in a planar obstacle model. There is no global
route search, navmesh, structural guidance or collision between agents. The
experiment does not add terrain, height or animation behavior.

## Workloads and parameter selection

The original and second-seed workloads reuse their exact prepared request
bytes: 1,000 independent start/goal pairs each, with seeds `1515146562` and
`1515146563`. Both have been studied in earlier experiments. Only the original
workload selects parameters in this sweep. The second seed is a reused
evaluation set, rather than a pristine holdout.

A third seed, `1515146564`, is declared before selection. Its 1,000 requests are
generated only after the winning configuration is locked and fingerprinted.
This tests unseen trips on the same map.
Request generation rejects physically blocked endpoints, without snapping
positions or filtering disconnected regions. Failed or impossible trips remain
in each 1,000-agent denominator.

Screening uses 128 original-workload requests at zero-based positions
`floor(i * 1000 / 128)`, for `i = 0..127`. Their original IDs and coordinates
remain unchanged. A full 1,000-agent baseline compatibility run first checks
every retained agent record and the geometry-query counts against the earlier
tangent run.

The search uses `kappa = weight / length²` as a numerical parameterization;
changing length at fixed kappa also changes weight. The declared coarse set
contains 56 distinct configurations:

| Group | Kappa | Length, m | Support, m | Configurations |
| --- | --- | --- | --- | ---: |
| Main grid | 16, 64, 256, 1,024 | 1, 2, 4 | 3, 6, 12, 24 | 48 |
| Inherited weight and length | 362.03867196751236 | 2 | 1.5, 3, 6, 12, 24, 48 | 6 |
| Weak-filter controls | 0, 1 | 2 | 12 | 2 |

The zero-weight control disables the normal-stage velocity-filter correction;
the field and tangent contour controller remain active. Both weak-filter
controls use the inherited length 2 m and support 12 m.

Configurations rank by most arrivals, then lowest whole-cohort mean
`min(1, remaining_distance / initial_distance)`. Deterministic ties prefer
smaller support, then length, then weight. CPU time never selects a winner.
The unclipped mean and median remaining-distance ratios are retained as
diagnostics; clipping is confined to the secondary selection score.

Refinement starts around the best two positive-kappa coarse configurations
whose summed absolute log2 distance in kappa, length and support is at least
two. Each center tests kappa multiplied and divided by two, and length and
support multiplied and divided by `sqrt(2)`. Length changes preserve kappa.
A center with 24 m support also tests 48 m. Duplicate configurations are removed;
the bounds are kappa `[0.25, 4096]`, length `[0.5, 8]` m and support `[1.5, 48]` m.

The best three distinct non-baseline subset candidates then run on all 1,000
original trips. The zero-weight control remains eligible if it ranks highly.
The winner is selected from those full-workload results and the inherited
full-workload baseline, using the same ranking. Keeping the baseline eligible
allows the experiment to retain its original parameters.

## Final comparison and validation

After the winner is locked, all six cohort/variant cases run afresh: baseline
and selected parameters on the original, second-seed and new holdout requests.
The two variants may have identical parameters if the inherited baseline wins;
both cases still run. Each case has one measured pass and zero warmups.

Screening and finalist selection retain native validation of every update but
omit the full movement log and independent stream audit. All six final cases
record every actual movement segment. The independent audit checks the complete
stream against the original padded geometry and strict map inset, including
record counts, agent IDs, continuity, endpoint safety and final-position
agreement. Decimated display traces are never used as a collision audit.

All six native validations and independent movement audits pass, covering
**268,373,526 actual movement segments**, with zero invalid movements or
geometry violations. Additional independent final QA reports 1,256,447 checks
and zero errors, rehashing all six movement streams and verifying the 78
selection/final execution naming and restoration records.

The comparison reports paired rescues and lost successes, common-success travel
and arrival-time ratios, remaining distances, contact categories, query counts
and native buffer memory. Contact labels use the original physical geometry
and include a separate map-boundary category. A small contact-label threshold
does not alter movement safety or shrink audited obstacles.

Sources, scene, original requests, binary and retained baseline references are
fingerprinted before selection. The winner and generated holdout bytes are
bound into the six final case descriptors. Every benchmark copy is renamed to
an executable basename containing `agent` or `npc`, without `zombie`, before
execution, then restored after exit, including failures. Final manifests retain
the actual command, renamed path, restored state and binary fingerprint.

Before the source freeze, 18 focused Python tests passed: eight for executable
naming/restoration and ten for candidate generation, refinement and selection.
A separate two-agent synthetic runner check independently audited all 198
movement segments, rendered its trajectories and confirmed executable-name
restoration. The full original-workload baseline reproduces the retained agent
records and query counts exactly. The selected original-workload final run also
reproduces its full selection-run agent records exactly.

## Timing scope

Screening workers run concurrently on separate configured CPU affinities.
Their timings are diagnostic and do not affect parameter ranking. Final cases
run sequentially on one configured CPU. The paired timing window uses the same
first 100 timesteps with all 1,000 agents active in both variants. Local steering,
geometry probes and strict movement are timed; setup, movement logging, traces
and independent audits are outside the update-batch timer.

The tables above distinguish matched-window CPU cost from total simulation CPU
and update count. Different parameters change trajectories, arrivals, stalls
and active lifetimes. Lower total CPU caused by earlier failure is not a useful
speed improvement. One pass without warmups provides exploratory timing,
without a statistical speed ranking. Native World and scratch-buffer figures
are not full process memory measurements.

## Implementation

The [retuning orchestrator](../tools/retune_potential_surface.py) records the
declared plan, subset, candidate results, refinement plan, finalists and winner.
The [comparison module](../tools/compare_potential_surface_retuning.py) checks
the six audited final cases and writes the outcome, paired and contact tables.
The [benchmark process helper](../tools/agent_benchmark_process.py) implements
temporary executable naming and restoration. The final retained run and its
evidence are under
[`build/potential-surface-retuning/20261022-current-tangent`](../build/potential-surface-retuning/20261022-current-tangent/plan.json).

- [Full comparison](../build/potential-surface-retuning/20261022-current-tangent/comparison.json), [locked winner](../build/potential-surface-retuning/20261022-current-tangent/winner.json), and [post-lock request/case provenance](../build/potential-surface-retuning/20261022-current-tangent/final-inputs.json).
- Tracked CSVs: [selection trials](potential-surface-retuning-screening.csv), [six final measurements](potential-surface-retuning-measurements.csv), [paired outcomes](potential-surface-retuning-paired-outcomes.csv), and [contact diagnostics](potential-surface-retuning-contacts.csv).
- [Plotting script](../build/potential-surface-retuning/20261022-current-tangent/render-retuning-report.py) and [plot input fingerprints](../build/potential-surface-retuning/20261022-current-tangent/plot-provenance.json). Charts are retained raster artifacts; they are not committed images.
- Original trajectories: [inherited baseline](../build/potential-surface-retuning/20261022-current-tangent/final/training-baseline/potential-surface-paths.png), [selected](../build/potential-surface-retuning/20261022-current-tangent/final/training-tuned/potential-surface-paths.png); independent audits: [baseline](../build/potential-surface-retuning/20261022-current-tangent/final/training-baseline/audit.json), [selected](../build/potential-surface-retuning/20261022-current-tangent/final/training-tuned/audit.json).
- Reused second-seed trajectories: [inherited baseline](../build/potential-surface-retuning/20261022-current-tangent/final/second-seed-baseline/potential-surface-paths.png), [selected](../build/potential-surface-retuning/20261022-current-tangent/final/second-seed-tuned/potential-surface-paths.png); independent audits: [baseline](../build/potential-surface-retuning/20261022-current-tangent/final/second-seed-baseline/audit.json), [selected](../build/potential-surface-retuning/20261022-current-tangent/final/second-seed-tuned/audit.json).
- Fresh-holdout trajectories: [inherited baseline](../build/potential-surface-retuning/20261022-current-tangent/final/holdout-baseline/potential-surface-paths.png), [selected](../build/potential-surface-retuning/20261022-current-tangent/final/holdout-tuned/potential-surface-paths.png); independent audits: [baseline](../build/potential-surface-retuning/20261022-current-tangent/final/holdout-baseline/audit.json), [selected](../build/potential-surface-retuning/20261022-current-tangent/final/holdout-tuned/audit.json).
