# Guardrail texture

`guardrail.png` is an original procedural 512 × 128 RGBA texture. One horizontal
repeat represents 2 meters: a shaded grey beam over an 8 cm post at the repeat
seam. Transparent pixels leave the space below the beam clear. The default
vertical span is 0.75 meters; mapped heights scale the whole texture vertically.

Posts and beam shading are pixels, with no separate post or beam meshes. The
generator embeds this source PNG in the guardrail GLB and shares its material
across spatial batches. The texture can be edited directly; regeneration of the
layer applies those edits. `src/guardrails.py:create_guardrail_texture` records
the procedural source for the supplied texture.

See [viewer preparation and controls](../../docs/VIEWER.md#mapped-guardrails).
