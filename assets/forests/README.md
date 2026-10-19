# Forest textures and bush billboard

`bush.png` is a transparent imagegen-generated summer woodland bush atlas. Its
left view supplies both perpendicular upright cards; its right view supplies
one horizontal card at quarter height. The loader measures each alpha silhouette
separately, finding the transparent divider near the image center, so source
padding does not affect the visible 4:1 width/height ratio. The native model is
4 m wide and 1 m tall and is randomly scaled to 6–14 m wide and 1.5–3.5 m tall
at placement time. The source texture is unchanged. All three cards
share a single mesh/material and have no collision. The final image was produced
with the built-in imagegen tool and a layout edit; both exact prompts are saved
in [bush.prompt.txt](bush.prompt.txt). See [old forest behavior](../../docs/FORESTS.md#old-forests-bush-understory-and-dry-trees).

`conservative_canopy_traces.json` contains the manually reviewed forest cores,
clearings and selected tracks in local east/north metres, plus the inset and
minimum-area parameters used for the active polygon configuration. See
[forest configuration and imagery provenance](../../docs/FORESTS.md).

`astra_canopy_traces.json` preserves the separate GPT-6 Astra satellite-only
review, with local east/north coordinates, review pixel coordinates, exclusions,
processing parameters and source raster fingerprints. It is independent of
the conservative orthophoto traces above. Rebuild the alternative root-level
`forest_patches_astra.json` with `tools/build_astra_forests.py`; use
`tools/compare_forests.py` for the offline satellite comparison. The existing
configuration remains available as the orthophoto preset. See the [alternative dataset instructions](../../docs/FORESTS.md#independent-astra-satellite-dataset).

`side.png` and `top.png` are generated opaque summer woodland textures for the
viewer runtime forest layer. They remain separate from the town GLB. The
side image repeats horizontally over 32 metres and covers the whole wall
height; the top image repeats over 32 by 32 metres in the horizontal plane.
The renderer uses shared sRGB textures, `GL_REPEAT`, and mipmaps.

Generated with the built-in imagegen tool. The prompts below were used with
opaque backgrounds. As generated photographic approximations, their tile
boundaries are not guaranteed to match perfectly.

Side prompt:

> Create a square seamless game texture tile for the SIDE of a distant
> temperate deciduous forest volume. Photorealistic dense Hungarian summer
> woodland, straight side elevation from ground to treetops, overlapping leafy
> mature broadleaf tree crowns occupying upper two-thirds, shaded trunks and
> dark forest interior at bottom, uneven organic canopy silhouette but image
> entirely filled with opaque foliage/tree interior, absolutely NO sky, NO
> horizon, NO ground plane, NO buildings, NO mountains, NO text. Dark natural
> muted green, subtle neutral lighting with no dramatic shadows, matching
> satellite summer woodland. Seamless left-right repetition, tree scale about
> 12 metres high, rich foliage detail. This image will repeat horizontally
> across a polygon forest wall. The very top edge should be leafy green and
> bottom edge dark woody forest; no borders.

Top prompt:

> Create a square truly seamless repeating game texture tile for the TOP of a
> distant temperate deciduous forest canopy. Photorealistic orthographic
> straight downward aerial view, only dense overlapping rounded leafy
> broadleaf summer crowns of mature Hungarian deciduous woodland, no gaps to
> bare ground, absolutely NO horizon, sky, trunks from side, roads, buildings,
> farmland, text or border. Dark natural muted greens with subtly varied
> individual crowns, soft neutral overhead daylight, no long cast shadows or
> bright patches. Must tile seamlessly in BOTH directions, crowns cut at every
> edge continue naturally on opposite edge, high density canopy surface, rich
> fine foliage detail. Tile represents about 32 x 32 metres of forest, crown
> diameters about 5-8 metres.

See [forest configuration and detail switching](../../docs/FORESTS.md).
