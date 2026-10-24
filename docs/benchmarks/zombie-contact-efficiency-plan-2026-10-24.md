# Zombie contact and ground efficiency experiments

Base: `d8026a7`; branch `experiment/zombie-contact-efficiency-20261024`.

1. Record the actual viewer at the fixed street camera and the current 1,600-zombie scenario. Save asynchronous frame CPU/GPU CSVs and a screenshot. Run detailed headless controls to separate static triangles, trunks, cache refresh, doors/props, static ground and prop support.
2. Try reusing the negative contact result obtained while lowering feet onto ground, avoiding an identical query immediately afterward. Keep all movement segments and support queries.
3. If triangles dominate, try a mathematically equivalent face-interior capsule/triangle fast path, retaining edge/vertex/general fallbacks. Reject it if geometry regressions or unexplained pose differences appear.
4. Try precomputing barycentric geometry in the bounded ground cache, preserving arithmetic and support ordering. Judge its memory/complexity cost against the measured ground and whole-update benefit.
5. If useful, test a smaller static-candidate patch. It trades fewer candidates against more BVH refreshes; keep it only if repeated controls improve.
6. Re-run the actual viewer and headless fixed-step comparisons for the selected version. Retain only measured wins; record rejected experiments and the remaining cost ceiling.

Keep criteria: meaningful repeatable whole-update benefit, focused movement/geometry/server checks, unchanged movement rules and final-pose comparisons. Ground still follows the rendered surface, including slopes, seams, caps and live props. Use alternate baseline/selected runs to limit ordering bias. Do not run CPU-heavy builds alongside timed runs. Parallelization remains a last resort.

No FPS or GPU gain is inferred solely from headless timing. GPU and CPU measurements overlap. Rendering comparisons retain lighting, camera and draw settings. A future reduction of query rate or simplified geometry is a separate approximation, assessed explicitly rather than silently introduced here.
