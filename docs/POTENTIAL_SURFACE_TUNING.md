# Potential Surface Steering parameter tuning

Follow-up to [`55b59db`](POTENTIAL_SURFACE_NAVIGATION.md), on branch
`experiment-zombie-potential-surface` in the fourth experiment workspace.
Only runtime field parameters change: potential weight, clearance falloff length,
and compact support distance. Physical geometry, the exact one-sided filter,
2 m/s speed, 0.05 s timestep, arrival radius, per-agent time budgets and stall
rules are unchanged. There is no pathfinding, tangent steering, contact sliding
or recovery rule in this comparison.

The later [shape and local-recovery experiment](POTENTIAL_SURFACE_RECOVERY.md)
changes those structural rules; the measurements below describe parameter tuning
of the original controller only.

**Parameter tuning improves completion on both trip sets, but does not make this
a reliable standalone navigator.** Original trips improve from 56 to 108 arrivals;
the untouched seed improves from 62 to 117. That is still only 10.8–11.7%
completion. Stronger fields also introduce slow off-contact stalls and longer
travel times. I would retain this as useful experimental evidence rather than
replace global pathfinding with this controller.

## Locked parameters and independent trips

| Parameter | Original | Selected |
| --- | ---: | ---: |
| Weight | 16 | 1448.1546878700494 |
| Clearance length scale | 1 m | 2 m |
| Compact support distance | 3 m | 12 m |
| Boundary resistance `weight / length_scale²` | 16 | 362.03867196751236 |

The selected setting is `--weight 1448.1546878700494 --length-scale 2 --support 12`.

The original 1,000 trips use seed 1515146562. Candidate parameters are selected
using those trips. The second set uses seed **1515146563**, chosen before
screening and generated only after `winner.json` locks the selection. It contains
1,000 new independent physically clear start/goal pairs on the **same map**,
without component filtering or endpoint snapping. It is a test of unseen trips,
not an independent map. Baseline and tuned variants receive byte-identical
requests within each set, including the same disconnected cases if any.

| Trips | Parameters | Arrived | Stalled | Timeout |
| --- | --- | ---: | ---: | ---: |
| Original / training | baseline | 56 | 944 | 0 |
| Original / training | tuned | 108 | 886 | 6 |
| Untouched seed | baseline | 62 | 938 | 0 |
| Untouched seed | tuned | 117 | 869 | 14 |

The per-agent budget remains `max(120 s, 3 * initial_distance / speed + 60 s)`.
A stall still means less than 0.05 m displacement from the current movement
anchor over 10 s. Larger weights can produce slow off-contact stalls or timeouts;
these are not silently removed or given longer budgets.

## Screening and selection

The source, binary, input hashes, configuration list and selection rules are
frozen before screening. The exposed default parameters reproduce **all 1,000
retained baseline agent records and every query count exactly**.

An evenly spaced 128-request subset uses original indices `floor(i*1000/128)`;
coordinates and IDs are retained. The coarse grid separates falloff scale from
effective boundary resistance using `kappa = weight / length_scale²`:

- `kappa`: 0.25, 1, 4, 16, 64, 256.
- Length scale: 0.5, 1, 2 m.
- Support: 1.5, 3, 6, 12 m.
- One weight-zero control, giving **73 coarse settings** in total.

The best two positive-strength coarse centers must have a summed absolute
log2 distance of at least 2 across `(kappa, length_scale, support)`. Each gets
six axis neighbors at factors `sqrt(2)` and `1/sqrt(2)`, plus doubled support
capped at 24 m; duplicates are removed. This adds **13 refinement settings**:
**86 settings screened** on the subset. Baseline finishes 4/128; the strongest
screened candidates finish 10/128. The weight-zero control finishes 1/128.

Ranking uses arrival count first, then lower whole-cohort median remaining /
initial goal distance, then ascending support, length scale and weight. CPU
timing does not select parameters. The three best positive-strength candidates
are evaluated on all original 1,000 trips. The compatibility baseline remains
eligible so the selection cannot lose training arrivals to a worse candidate.

| Finalist: weight, length, support | Arrived / 1,000 | Stalled | Timeout |
| --- | ---: | ---: | ---: |
| 2048.000000, 2.828427 m, 12 m | 105 | 877 | 18 |
| 1448.154688, 2.000000 m, 12 m | 108 | 886 | 6 |
| 1024.000000, 2.000000 m, 24 m | 108 | 884 | 8 |

The winner is locked before any second-seed outcomes are examined. No parameters
are changed after independent validation. This is a bounded search within the
existing field family, not proof of globally optimal parameters.

## Gains, regressions and route quality

| Trips | Both arrive | Baseline only | Tuned only | Neither |
| --- | ---: | ---: | ---: | ---: |
| training | 54 | 2 | 54 | 890 |
| holdout | 57 | 5 | 60 | 878 |

The tuned parameters lose 2 original successes and 5 independent-seed successes;
the gains are not universal improvements for every individual request.

Route quality compares only agents that arrive with both parameter sets within
the same workload. The changed success population is reported separately.

| Trips | Common successes | Median travel change | Median simulated arrival-time change |
| --- | ---: | ---: | ---: |
| training | 54 | +4.63% | +49.60% |
| holdout | 57 | +3.54% | +43.50% |

Arrival is still within 0.15 m of the endpoint, so travel / straight-distance
ratios can be slightly below one. Global reachability is not classified; neither
the training nor validation failure totals establish that every failed trip had
a possible route.

## CPU and failure diagnostics

Final validation runs one measured pass per variant and trip set, with full
movement recording and independent auditing. Preliminary full-set screens also
exercise the selected parameters, but do not record every movement. The final
CPU figures are exploratory **single-pass observations**, not confidence bounds
or a statistical speed ranking. This keeps expensive deterministic kilometer-
scale simulations bounded while preserving all physical validation.

Fixed-load CPU samples use the common first-five-second timestep keys where
both variants still have all 1,000 agents active. The two variants therefore
use exactly the same timing window. Timers run once per batch, not per agent;
native validation, full movement logging, display traces and parsing remain
outside the update timers.

| Trips | Parameters | Mean CPU / 1,000-active tick | CPU tick p95 | Whole-trip update CPU | Whole-trip update elapsed |
| --- | --- | ---: | ---: | ---: | ---: |
| training | baseline | 1.659 ms | 3.335 ms | 9.783 s | 22.534 s |
| training | tuned | 2.040 ms | 3.988 ms | 43.480 s | 96.394 s |
| holdout | baseline | 1.478 ms | 2.209 ms | 8.082 s | 18.425 s |
| holdout | tuned | 1.882 ms | 2.556 ms | 43.766 s | 96.399 s |

Each fixed-load timing row contains exactly **100 matched ticks**, steps 0–99.
On the independent seed, observed fixed-load CPU rises by **27.3%**. At 20 Hz,
the tuned update work corresponds to about **3.76% of one core**, excluding all
other game work. Whole-trip update CPU rises from 8.082 s to 43.766 s because
agents remain active longer and evaluate broader fields. Shared-world storage
remains 24,150,920 B; no path/search storage is added.

The whole-trip CPU total depends on how long agents remain active. An increase
can reflect more movement or slower progress; an early-stalling controller can
have a misleadingly cheap whole-pass total. These figures are also not initial
pathfinding request times or full-game frame times.

The final endpoint diagnostic uses the nearest original padded obstacle or
inset map boundary. Near contact means within 10 micrometers for this diagnostic
only; the collision auditor never uses that threshold or shrinks geometry.

| Trips | Parameters | Building contact | Tree contact | Fence contact | Off contact |
| --- | --- | ---: | ---: | ---: | ---: |
| training | baseline | 627 | 10 | 307 | 0 |
| training | tuned | 430 | 4 | 175 | 277 |
| holdout | baseline | 620 | 12 | 306 | 0 |
| holdout | tuned | 386 | 10 | 188 | 285 |

All 277 training and 285 independent-seed off-contact stalls have **zero clipped
moves**. Their current failure is slow progress under the soft field and the
unchanged movement threshold, rather than collision clipping. All remaining
stalled agents have clipped moves. There are no map-boundary stalls. A separate
GEOS distance recomputation confirms these contact categories.

Parameter tuning changes anticipation and resistance but leaves the structural
limit intact: the filter preserves positive velocity toward the goal, and
clipping only shortens that movement. It cannot deliberately retreat. Soft
penalties, combined gradients and approximate building/fence fields can also
leave the agent pressing against a physical obstacle. A slower off-contact stall
does not demonstrate improved route finding.

## Verification and reproduction

The two C++ suites pass, and ten CLI checks exercise invalid finite/range values,
zero weight and output-control flags. The unchanged Python auditor retains its
six regression tests, including GEOS comparisons and malformed stream rejection.
Every screening and final run retains native geometry validation at every
update. Disabling movement logging and aerial copying during screening changes
output work only; screening runs do not claim an independent stream audit.

The four final runs validate **57,733,571 agent updates** natively, and their
independent audits check **57,157,684 complete moved segments**, with zero
collisions, boundary violations, invalid IDs, nonfinite coordinates, stream
discontinuities or final/count mismatches. No penetration tolerance is applied.
The selected training run reproduces the earlier full-set finalist exactly,
including every agent record and query count. The baseline likewise reproduces
the committed original. Independent-seed runs are single-pass observations;
repeatability there is not claimed from multiple measured passes.

Runtime sources and executable hashes remain unchanged during each frozen run.
The physical core and simulator are unchanged from the committed prototype.
CLI/API defaults remain weight 16, length scale 1 m, support 3 m for exact
reproduction of the original experiment; tuned values are requested explicitly.

Build the standalone target as described in the original report, then run the
tracked tuning orchestrator with the retained original request file:

```bash
.venv/bin/python tools/tune_potential_surface.py \
    --binary build/potential-surface/benchmark_potential_surface \
    --scene /path/to/original/paths.json \
    --requests /path/to/original/requests.json \
    --baseline-summary /path/to/original/simulation/summary.json \
    --output build/potential-surface-tuning/new-run --subset 128 --cpu 2
```

Then run the baseline and locked settings against the original requests and
fresh seed 1515146563 requests using `tools/run_potential_surface_experiment.py`:
`--requests FILE` reuses exact request bytes; `--weight`, `--length-scale`, and
`--support` select the field parameters. Keep full logging enabled for audited
runs. `--no-move-log --no-aerial` are screening-only output controls.
`tools/compare_potential_surface_tuning.py` produces paired completion, quality,
CPU and contact comparisons from the four final directories. A complete final
command plan is retained with the run.

The retained run is [`build/potential-surface-tuning/20261021T125220Z`](../build/potential-surface-tuning/20261021T125220Z/manifest.json).

- [Screening configuration/source manifest](../build/potential-surface-tuning/20261021T125220Z/manifest.json), [refinement plan](../build/potential-surface-tuning/20261021T125220Z/refinement-plan.json), [locked winner](../build/potential-surface-tuning/20261021T125220Z/winner.json), and [final command plan](../build/potential-surface-tuning/20261021T125220Z/final/plan.json).
- [Complete paired comparison](../build/potential-surface-tuning/20261021T125220Z/final/comparison.json), [tracked screening CSV](potential-surface-tuning-screening.csv), and [tracked final measurement CSV](potential-surface-tuning-measurements.csv).
- Original trip images: [baseline](../build/potential-surface-tuning/20261021T125220Z/final/training-baseline/potential-surface-paths.png), [tuned](../build/potential-surface-tuning/20261021T125220Z/final/training-tuned/potential-surface-paths.png).
- Untouched-seed images: [baseline](../build/potential-surface-tuning/20261021T125220Z/final/holdout-baseline/potential-surface-paths.png), [tuned](../build/potential-surface-tuning/20261021T125220Z/final/holdout-tuned/potential-surface-paths.png).
- Independent audit JSON: [training baseline](../build/potential-surface-tuning/20261021T125220Z/final/training-baseline/audit.json), [training tuned](../build/potential-surface-tuning/20261021T125220Z/final/training-tuned/audit.json), [holdout baseline](../build/potential-surface-tuning/20261021T125220Z/final/holdout-baseline/audit.json), [holdout tuned](../build/potential-surface-tuning/20261021T125220Z/final/holdout-tuned/audit.json).

Each final directory contains exact requests, a source/binary manifest, complete
agent outcomes, per-step CPU/elapsed measurements and a full movement stream.
Images, raw streams and binaries remain ignored build artifacts; no images are
added to Git.
