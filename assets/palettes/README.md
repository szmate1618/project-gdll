# Undead palette

The user-supplied [reference image](../undead_dog/palette-reference.png) is
preserved in the workspace. [undead.json](undead.json) stores reusable sRGB
colors as integer RGB triples in the range 0–255, with a SHA-256 fingerprint of
that reference. Schema version 1 separates these two groups:

| Group | Colors | PNG swatches | Purpose |
| --- | --- | --- | --- |
| `surface_rgb` | 53 | [undead-surface.png](undead-surface.png), 53×1 | Skin, fur, blood, bone and dark neutral surfaces |
| `emissive_rgb` | 42 | [undead-emissive.png](undead-emissive.png), 14×3 | The reference's separate labeled emissive strip |

The PNG swatches contain exactly the saved RGB values. Enlarge with nearest
neighbor when inspecting them. The surface colors are sorted and deduplicated;
the emissive array and image retain the reference's row and column order.
Colors were sampled from small swatch interiors with a median to avoid the
reference image's blurred boundaries. Black padding and the label are excluded.
The original reference remains the record of the supplied layout.

An emissive swatch is a color choice, not an instruction that makes a material
glow. The undead dog variants use only `surface_rgb` and ordinary lit materials.
Their off-white ribs use **RGB 209, 204, 201**, hexadecimal **#D1CCC9**.

The [dog preparation tool](../../tools/prepare_undead_dog.py) loads the saved
surface colors and verifies them against the reference before preparing assets.
Its named coat mappings in [undead_dog_variants.py](../../src/undead_dog_variants.py)
select existing swatches directly; they never average colors or apply runtime
brightness multipliers. See the [dog asset guide](../undead_dog/README.md) for
the three appearances, source license and offline rebuild command.
