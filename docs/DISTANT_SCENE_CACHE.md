# Experimental distant scenery rectangle

Enable with `--distant-scene-cache` on `godollo_viewer` or the multiplayer game
client. It defaults off. Scopes bypass it. No generation, collision, simulation,
network protocol or resource fingerprint changes are involved.

Press **F7** during gameplay in either app to enable or disable it immediately,
including while driving. Each press prints the new state to the console. The
choice lasts for the current viewer/session; the startup flag sets its initial
state. Enabling captures a fresh image on the next render with distant scenery.

This version replaces the tiled cubemap with one screen-sized RGBA8 texture,
updated at **15 Hz**. Distant scenery is drawn once from the current camera on
each refresh. The existing camera draw list is reused, so there is no extra
town visibility query for the capture. Between refreshes, four vertices place
that image on a rectangle fixed to the capture camera at a **1 km proxy depth**.
The current camera projects those vertices; the fragment shader samples only
color. The rectangle fills the screen at capture time.

Turning or moving can expose empty image margins. These gaps wait for the next
scheduled refresh; there is no patching or extra geometry pass on turns. Camera
movement uses the rectangle's single proxy distance, so parallax is approximate.
The capture has its own depth renderbuffer for correct surface ordering within
the image. It uses reversed FP32 depth when the camera pass does, retaining the
precision needed by 1 mm terrain overlays; conventional modes use depth24.
Its depth is never sampled or written into the main scene.
Normally rendered actors and other foreground geometry always draw over it,
even when they would physically be behind distant cached scenery. These are
intentional approximation limits. Visual review is left to the user.

Static opaque/masked town geometry, trees and forest volumes whose complete
conservative bounds lie at least **1,000 m** from the refresh position enter the
capture. Bounds crossing that distance stay at full rate; complete building
batches share one decision. Translucent surfaces, movable panels, actors,
vehicles, props, effects, shadows and the HUD retain their normal cadence.
Views without distant static candidates bypass capture allocation and drawing.
The camera visibility list may represent a complete roof or distant wall batch
with one source ID. Cache eligibility uses the complete batch bounds rather than
that representative's bounds, including when deciding whether any distant work
exists. Visible-object counters still include all selected sources.

Both cached and full-rate town passes use the specialized town shader. Its
lighting and camera uniforms are prepared before a capture refresh, including
glass-only scenes. Each pass restores the full shader and camera matrices before
drawing instanced trees and forest volumes.

The existing terrain/road/railway/fence index partitions remain: map-wide meshes
have 128 m cells with conservative bounds and a BVH, sharing original vertices
and transforms. Long triangles stay whole. These partitions also let nearby
sections remain at full rate. Disabled rendering and shadows use original lists.
Each source mesh submits its selected chunk ranges with one `glMultiDrawElements`
call per pass, preserving range order. Reusable CPU argument arrays avoid frame
allocations. Draw counters count GL submissions, including each multi-draw once.

At 1280x720, one RGBA8 image plus a depth24 renderbuffer with four-byte storage
uses approximately **7 MiB**, compared with approximately 75 MiB for the earlier
cubemap. Linear filtering smooths the projected image. No per-pixel viewing-ray
rotation or depth conversion remains in the composite shader.
Changing the camera depth convention reallocates and refreshes the capture.
CPU visibility and rectangle placement retain conventional projection matrices;
only the capture's GPU projection follows reversed depth. Composition writes no
depth and restores the active camera comparison before foreground drawing.

Refresh uses an explicit presentation clock when supplied, otherwise monotonic
wall time. The viewer supplies simulation time independently of frozen animation
poses. Teleports over 20 m, resizing, clock resets and toggling force a refresh.
Other image changes can lag by about 67 ms. Refresh work can still cause spikes.

The earlier tiled implementation is preserved in commit `a3eb387` on
`experiment/distant-scene-cache`; its [benchmark report](benchmarks/distant-scene-cache-2026-11-02.txt)
remains available. See [benchmark instructions](BENCHMARKS.md#experimental-distant-scenery-cache)
for the current capture harness.
