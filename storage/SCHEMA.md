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
| `access_time` | Finite nonnegative compartment access time in seconds |

Backpacks also have a `[backpacks.grid]` table with `width`, `height`,
and `access_time`. The main compartment's base is the former bottom-row time:
sling bag 1.22s, tactical backpack 3.08s, hiking backpack 4.45s. There is no direct
row/depth surcharge; `depth_time_per_row` is no longer used. External pockets and
body storage retain their fixed access times.

Main backpack grids also accept `obstruction_time_per_cell` and
`support_time_per_cell`. Specify both, as finite nonnegative seconds per score
point, or omit both for fixed access. Starter values reuse the former row
increments: 0.08s for the sling bag, 0.12s for the tactical pack, 0.15s for the
hiking pack, for both penalty and bonus.

For every cell of the item's rotated footprint, count occupied cells anywhere
above and below it in the same column, including across empty gaps. Exclude the
queried item itself. **Obstruction** is the largest above-count; **support** is
the average below-count across all footprint cells, including columns with zero
support. Access time is:

```text
max(0, access_time + obstruction * obstruction_time_per_cell
                   - support * support_time_per_cell)
```

Pickup snapshots the source scores before removing the item. Placement scores
the proposed destination. A swap excludes both exchanged items: the incoming
item uses its own destination scores, and the outgoing item snapshots its own
source scores before the incoming item is stored. Automatic pickup costs no time;
its source cost is deferred until that item's next placement, like manual pickup.

Picking up an item is immediate. A valid drop starts a timer for the original
compartment's base plus the destination compartment's base, charging the base
only once if both are the same compartment. Add obstruction penalties and subtract
support bonuses for both the source and destination backpack footprints, then
floor the combined time at zero. Returning to the exact pickup compartment, cell
and rotation is free if no other item is displaced, including inside the backpack.
Other moves within one compartment charge one base. Simple GUI clicks select details without picking up or replacing
the item.
Timed placements stay pending until the timer completes, with a progress bar beside the
destination. During placement, further moves/rotation are blocked; cancellation
remains available. An overlapping item is picked up automatically at completion
and pays its source and destination costs on its next placement. Cancelling an unfinished
chain restores all its original placements immediately. The game continues to
run, including time progression, while inventory is open.

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
Automatic packing is not implemented. Item details follow the selected item.
The shared hotbar reads owned item IDs and `skill:<ID>` shortcuts from root
`inventory.toml`; skills are registered in `skills.toml`. Gameplay previews
these slots and confirms a timed weapon switch with left click. Unknown extra catalog fields are ignored, so tags from an older
storage descriptor have no effect.

The equipment/backpack catalogs and `loadout.toml` configure the storage view.
Their bytes are included in multiplayer fingerprints because weapon switch
timing uses the configured storage layout. The weapon
files in the `weapons/` subfolder are shared content: gameplay TOMLs, inventory
TOMLs and tag sidecars are included in fingerprints and content packs. See
[weapon configuration](weapons/README.md) for their independent schemas.

Root `inventory.toml` continues to control ownership, ammunition, placements and
shortcuts. Gameplay behavior is defined in `storage/weapons/*.gameplay.toml`;
inventory names and footprints use `*.inventory.toml`, and tags use `*.tags.txt`.
