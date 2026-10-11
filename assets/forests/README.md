# Distant forest textures

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
