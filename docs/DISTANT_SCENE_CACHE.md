# Experimental distant scenery cache

Enable with `--distant-scene-cache` on `godollo_viewer` or the multiplayer game
client. It defaults off. Scopes bypass the cache. No generation, collision,
simulation, network protocol or resource fingerprint changes are involved.

Views without visible distant static candidates bypass allocation, tile draws
and composition entirely. The normal camera draw list is reused for this check;
foliage uses spatial queries. Moving actors alone never activate the cache.

Static opaque/masked town geometry, individual trees and forest volumes whose
complete conservative bounds lie at least **1,000 m** from the refresh position
enter a **15 Hz** cache. Bounds crossing that distance stay at full rate; complete
building batches share one decision. Translucent surfaces, movable door panels,
actors, vehicles, props, effects, shadows and the HUD keep their normal cadence.

The cache stores color and depth in cubemap faces subdivided into 256-pixel
tiles. Each display frame samples those directions using the current camera
rotation, without repeatedly resampling previous images. When turning exposes
an uncached tile, only that tile is rendered immediately. Cropped tile frusta
query the existing spatial indexes; already cached directions issue no distant
draws. Returning to a previously seen direction reuses its original tiles until
the next scheduled refresh.

Translation is held at the refresh position. Small parallax errors from walking
can remain for up to about 67 ms. There is no per-frame interior-hole repair.
Teleports over 20 m force an immediate refresh. A reset presentation clock,
cache toggle or texture resize also invalidates the cache. Refresh uses an
optional explicit presentation clock, otherwise monotonic wall time. The viewer
supplies simulation time independently of `--animation-time`; fixed-step
benchmarks explicitly supply their 60 Hz timeline in both modes.

Map-wide terrain, roads, railways and fence meshes are partitioned once into
128 m cells by triangle centroid. The original vertex buffers and transforms
are shared; index subsets have conservative bounds and a BVH. Triangles are
never cut or reprojected into new world geometry. Long triangles crossing cells
can consequently stay near even when their centroids are far. The ordinary
disabled renderer and shadow passes retain their original index lists.

At 1280x720 with the normal 60-degree field of view, cubemap faces are 1280x1280,
using approximately 75 MiB for six RGBA8/depth24 faces with four-byte depth
storage. Nearest sampling avoids reading unfilled neighboring tiles but can
alias distant edges. Full refreshes can spike frame cost; caching is not an
assumption of a speedup. Lighting and distant vegetation changes also wait for
the scheduled refresh. Existing sunlight shadow work is not cached.

Implementation: `src/distant_scene_cache.hpp/.cpp` owns cache targets, angular
coverage, refresh and composition; `src/renderer_distant.cpp` handles static
geometry partitioning, bounds and draw integration. See the
[benchmark instructions](BENCHMARKS.md#experimental-distant-scenery-cache).
