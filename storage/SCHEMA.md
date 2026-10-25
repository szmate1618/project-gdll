# Configurable storage prototype (v1)

`equipment.toml` and `backpacks.toml` are catalogs of possible storage types.
`loadout.toml` selects the player's equipped catalog IDs:

```toml
schema_version = 1
backpack = "tactical_backpack"
equipment = ["hip_holster", "chest_pouch", "left_shoulder_sling", "right_shoulder_sling"]
```

The starter bag is the **8×10 tactical backpack**, large enough for the three
large weapons placed in it. Other choices are `small_sling_bag` (4×5) and
`hiking_backpack` (10×14).
Omit `backpack` for no backpack; use `equipment = []` for no body storage.

The application reads all three files **when creating the player inventory**, then
keeps that model for the session. Restart the application to reload edits. The folder lives
beside `ui/` in the checkout and distributed clients. Missing files and invalid
values produce an error naming the source file and field.

## Catalog fields

Each storage catalog and loadout file has integer `schema_version = 1`. Each `[[equipment]]` or
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

## Shoulder slings

`left_shoulder_sling` and `right_shoulder_sling` each define a vertical **2×5**
`carry` compartment, with a prototype access time of 0.7 seconds. Both are
equipped in the starter loadout and appear at the top corners of Body Storage.
Their references are `body/left_shoulder_sling/carry` and
`body/right_shoulder_sling/carry`.

They fit the 5×2 sniper when rotated 90° using the mouse wheel while held.
The sniper starts in the backpack. The chest
pouch's panel position is lower to leave room for the right sling.

## Item tag restrictions

Each compartment may specify `allowed_tags = ["handgun"]` alongside its width,
height and access time. An item must have at least one of the listed tags in its
`*.tags.txt` definition. Omit the field or use `[]` to accept any item. Entries
must be unique, nonempty strings containing lowercase letters, digits or underscores.
The same optional field works inside a backpack's main `grid` table.

The hip holster accepts `handgun`; both shoulder slings accept `long_gun`.
The chest pouch and backpacks remain unrestricted. Both startup placement
validation and drag/drop previews enforce the rule, including swaps and rotated
items. Rotation never changes item tags. The item must also fit the grid.

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

Item placements live in root `inventory.toml`, referencing equipped storage
with `body/<equipment-id>/<slot-id>`, `backpack/<backpack-id>/main`, or
`backpack/<backpack-id>/<slot-id>`. `main` is reserved for the internal grid.
Unlike compartment anchors, **item positions are integer cell coordinates** of
the footprint's top left. Dimensions come from the item definition. The UI
requires a valid non-overlapping placement for every owned item; it does not
rearrange items to make them fit. See [the item schema](weapons/README.md).

Dragging supports rotation, free-space placement and exchange with one overlapping
item. Right-click, closing Inventory or losing focus restores the entire unfinished
swap chain; placing into free space completes it. Moves last for the session only.
Optional placement `rotation` is 0, 90, 180 or 270 degrees, clockwise.
Automatic packing and equipping interactions are
not implemented. Item details follow the selected item. The shared hotbar reads weapon
ownership and shortcuts from root `inventory.toml`; Skills entries remain visual
examples. Unknown extra catalog fields are ignored, so tags from an older
storage descriptor have no effect.

The equipment/backpack catalogs and `loadout.toml` configure the storage view.
They remain local UI resources outside multiplayer fingerprints. The weapon
files in the `weapons/` subfolder are shared content: gameplay TOMLs, inventory
TOMLs and tag sidecars are included in fingerprints and content packs. See
[weapon configuration](weapons/README.md) for their independent schemas.

Root `inventory.toml` continues to control ownership, ammunition, placements and
shortcuts. Gameplay behavior is defined in `storage/weapons/*.gameplay.toml`;
inventory names and footprints use `*.inventory.toml`, and tags use `*.tags.txt`.
