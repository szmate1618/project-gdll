# Configurable storage prototype (v1)

`equipment.toml` and `backpacks.toml` are catalogs of possible storage types.
`loadout.toml` selects the player's equipped catalog IDs:

```toml
schema_version = 1
backpack = "small_sling_bag"
equipment = ["hip_holster", "chest_pouch"]
```

The starter bag is **4 columns × 5 rows**, with one external pocket. The other
backpack choices are `tactical_backpack` (8×10) and `hiking_backpack` (10×14).
Omit `backpack` for no backpack; use `equipment = []` for no body storage.

The HUD reads all three files **on first opening Inventory**, then keeps that
screen for the session. Restart the application to reload edits. The folder lives
beside `ui/` in the checkout and distributed clients. Missing files and invalid
values produce an error naming the source file and field.

## Catalog fields

Every file has integer `schema_version = 1`. Each `[[equipment]]` or
`[[backpacks]]` entry has a unique nonempty `id`, a nonempty display `name`, and
`[[equipment.slots]]` / `[[backpacks.slots]]` compartment tables. To define no
external compartments, set `slots = []` directly on the backpack entry.
Each compartment has:

| Field | Meaning |
| --- | --- |
| `id`, `name` | ID unique within its parent, and display label |
| `x`, `y` | Finite normalized panel coordinates in `[-1, 1]` |
| `width`, `height` | Integer grid dimensions, 1–16 cells per axis |
| `access_time` | Finite nonnegative retrieval time in seconds |

Backpacks also have a `[backpacks.grid]` table with `width`, `height`,
`access_time`, and `depth_time_per_row` (finite nonnegative seconds). Its top row
has the base access time. Row `r`, counting from zero, adds
`r * depth_time_per_row`. The screen displays the top and bottom row times.
These are descriptive values; no retrieval mechanic is implemented.

## Coordinates and layout

Coordinates position the compartment card's **center within the inner storage
rectangle of the Body Storage panel or Backpack panel**. They are independent of
the body placeholder and the main backpack grid. X increases rightward; Y
increases downward. `(0, 0)` is the rectangle's center, `(-1, -1)` its top left,
and `(1, 1)` its bottom right.

The inner rectangle excludes the heading and footer: its origin is `(20, 62)`
design dp from the panel's top left, with a height of 344dp. Body Storage is
348dp wide inside; Backpack is 388dp wide inside. The whole UI scales with the
viewport. Cards are clamped at the rectangle's edges to keep their label, grid,
and access time inside. Authors must choose coordinates that avoid overlaps;
there is no automatic packing. The main backpack grid is centered and its cell
size adapts to its dimensions. External compartments use the whole inner panel,
not coordinates relative to that central grid.

## Prototype boundary

This simplifies the supplied descriptor schema: item tags, fitting, rotation,
saved placements and equipping interactions are not modeled.
All compartment cells start empty. Item details remain unselected; the shared
hotbar reads weapon ownership and shortcuts from root `inventory.toml`; Skills
entries remain visual examples. Unknown extra TOML fields are
ignored, so tags from an older descriptor have no effect.

This folder configures the **storage view only**. The existing root
`inventory.toml` and `weapons/*.toml` control prototype weapon ownership and
ammo independently. [Item descriptors](../items/README.md) in `items/*.toml`
define inventory labels, cell footprints and tags. Storage files are local UI resources and are not added
to multiplayer gameplay fingerprints.
