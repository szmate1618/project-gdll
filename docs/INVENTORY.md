# Inventory code and ownership

The inventory model has no OpenGL, RmlUi or window dependencies. Its CMake
targets are defined in `cmake/Inventory.cmake` for graphical and headless builds.

| Responsibility | Module |
| --- | --- |
| Item definition with inventory/gameplay components and shared tags | `src/inventory/item.hpp/.cpp` |
| Validate and atomically save complete crafted definitions and local palm frames | `src/inventory/crafted_item.hpp/.cpp` |
| Read item descriptors, tags and optional weapon gameplay | `src/inventory/item_config.hpp/.cpp` |
| Parse weapon gameplay settings for both items and combat | `src/weapon_config.hpp/.cpp` |
| Read player ownership, placements and shortcuts | `src/inventory/inventory_config.hpp/.cpp` |
| Read storage catalogs and equipped loadout | `src/inventory/storage_config.hpp/.cpp` |
| Own player items, equipped storage, placements and held item; answer queries | `src/inventory/player_inventory.hpp/.cpp` |
| Resolve compartments and validate footprints, tags and overlaps | `src/inventory/inventory_placement.hpp/.cpp` |
| Score backpack obstruction/support and calculate adjusted access times | `src/inventory/inventory_access.hpp/.cpp` |
| Pick up, rotate, place, swap and cancel a placement transaction | `src/inventory/inventory_drag.hpp/.cpp` |
| Own physical ground items and validate transfers within two metres | `src/ground_items.hpp/.cpp` |
| Serialize dynamic inventory ownership and ground replicas | `multiplayer/inventory_state.hpp/.cpp` |
| Draw storage grids and item details | `src/gui/inventory_view.hpp/.cpp` |
| Convert pointer/wheel input and hit tests into inventory operations | `src/gui/inventory_interaction.hpp/.cpp` |
| Present configured action shortcuts | `src/gui/action_hotbar.hpp/.cpp` |
| Preview and confirm ten action shortcuts | `src/action_selection.hpp/.cpp` |
| Calculate and advance timed weapon changes | `src/weapon_switch.hpp/.cpp` |
| Select ammo stacks and advance timed reloads | `src/weapon_reload.hpp/.cpp` |
| Coordinate GUI documents and rendering | `src/gui/hud.hpp/.cpp` |

## Item components

`Item` is an immutable definition. `inventory()` returns its display name, type,
N×M footprint, quantity unit and optional physical model, parsed from
`<id>.inventory.toml`. `gameplay()` returns the
actual `WeaponConfig`, parsed from `<id>.gameplay.toml`, including firing/ammo
settings and grenade parameters. `tags()` and `hasTag()` use the descriptor's
optional inline `tags` array, falling back to `<id>.tags.txt` when absent.
Inline tags are authoritative; the two sources are never combined.
Combat's `Weapons` uses the same gameplay parser; there is no second inventory
interpretation of the gameplay schema.

`Item::withCrafting()` creates an immutable, nonstackable equippable definition
from primitive geometry and local `ItemGrip` frames. Positions are metres in the
model's Y-up frame; grip +Z points into the object and +Y points toward the
knuckles. Primitive rotations are local quaternions. Complete model, material,
and grip data travel with inventory/ground records and the retained equipped
custom item, including when no backpack space is available. Saved definitions
use schema-versioned TOML under `userdata/crafted_items/` and validate a temporary
file before replacing the prior definition.

Mutable magazine rounds and cooldowns remain in `Weapons`. Reserve ammunition
is the quantity of matching ammo stacks in `PlayerInventory`; combat's reserve
count is a display cache. Mutable storage locations also belong to that model. Changing one player's placement
does not change an item's definition.

## Player model and queries

`loadInventoryContents(root)` reads configuration data without creating GUI
objects. `loadPlayerInventory(root)` also loads equipped storage and constructs
a validated model. Every owned item must start in exactly one valid compartment.
The standalone application and graphical multiplayer session own their model;
`Hud` receives a borrowed reference. Closing or recreating GUI documents does
not reload configuration or reset completed moves. Keep the model alive until
the HUD and any placement transaction controllers have been destroyed.

```cpp
auto inventory = viewer::loadPlayerInventory(resourceRoot);
bool havePistol = inventory.owns("pistol");
bool haveLongGun = inventory.hasTag("long_gun");
bool haveWeapon = inventory.hasType("weapon");
const viewer::Item* pistol = inventory.find("pistol");
auto location = inventory.locationOf("pistol");
auto longGuns = inventory.findByTag("long_gun");
auto weapons = inventory.findByType("weapon");
auto backpackItems = inventory.itemsIn("backpack/tactical_backpack/main");
```

Unknown item IDs produce `false`, `nullptr` or an empty optional. Query lists
contain borrowed item pointers and value copies of their current locations.
Do not retain a location snapshot and expect it to update after a move.
`InventoryLocationKind::Storage` includes the compartment, cell position and
rotation. `InventoryLocationKind::Hand` has no stored placement. A held item is
still owned and appears in tag/type queries, including immediately after a swap.
Read-only accessors expose the complete item map, placements, layout and configured
shortcuts when a caller needs to enumerate them.

Hotbar references are owned item IDs or `skill:<ID>`. Registered skill labels
are loaded from `skills.toml` separately from stored item ownership. Skills
occupy no storage. Unknown references are rejected.

`InventoryDrag` borrows the model and is the placement transaction service; it
contains no pointer coordinates or GUI code. The GUI uses it, and gameplay can
use the same operations. Only the controller that began a transaction can finish
or cancel it. Cancelling or destroying that controller restores the entire
unfinished chain; completed free-space drops persist. Storage and item maps
cannot be modified directly through the public model API.

Pickup is immediate. `drop()` validates the destination and starts `pending()`
for the source and destination compartment bases (one base if both references
identify the same compartment), plus both backpack endpoint adjustments.
Returning to the exact pickup compartment, cell and rotation is free if it
displaces no other item. Moving elsewhere in that compartment charges its base once.
`advance(seconds)` takes
elapsed simulation/session seconds and commits the placement when time is up.
Pending items still report `Hand` until completion. A displaced item is picked
up at completion for free and retains its source access for its next placement,
just like a manually picked-up item.
Further drops/rotation are blocked during a pending placement. `cancel()` needs
no time and restores the whole unfinished chain. The GUI consumes the caller's
`HudState::seconds` clock, which continues while menus are open, and draws a
progress bar beside the pending destination. There is no game pause.

The GUI separates selection from pickup: a press selects details, and a release
without dragging does not modify the model. A drag starts after 4 design pixels
of movement, or a deliberate wheel turn while pressed. Pointer paths are retained
so moving away and back between rendered frames still counts as a real drag.

`inventoryAccess()` is a pure occupancy calculation used by source pickup,
destination placement and the item details display. Backpack main access starts
from the catalog's base time, adds a linear obstruction penalty and subtracts a
linear support bonus, floored at zero. Obstruction is the maximum occupied-cell
count above any footprint cell; support is the mean count below footprint cells.
Self is excluded; empty gaps do not stop the scan. For a swap, both exchanged
items are excluded. The incoming item uses its own destination footprint; the
outgoing item snapshots its own source footprint before the incoming item is
published, and pays that cost on its next placement. Non-main compartments
remain fixed. Pending placements retain source/destination scores and standalone
access times as snapshots for inspection by callers.

`inventoryTransferSeconds()` combines these scores for inventory moves and weapon
switches: add each distinct compartment's base once, add the source and
destination obstruction penalties, and subtract their support bonuses. Floor
the combined storage cost at zero, after all adjustments. Identical grid pointers
identify a shared compartment; null endpoints have no access cost (fists).
Standalone endpoint times cannot simply be added because their bases may be shared.

## Ammo stacks and reloads

Ammo descriptors in `storage/ammo/` define `type = "ammo"`, `ammo_type`,
`stackable = true` and `max_stack`. Root `inventory.toml` declares each stack
under `[ammo.<unique-instance-id>]`, with `item` naming its descriptor and
`quantity` between zero and its maximum. Each instance needs a placement.
Multiple stacks may share a descriptor. Empty stacks stay owned, occupy their
cell and remain movable. Dragging a compatible stack onto another merges up
to the target maximum and restores the source to its pickup position, including
when its quantity becomes zero.

`WeaponReload` selects a positive matching stack that is currently stored.
The shortest `inventoryAccess()` time wins, including backpack obstruction and
support. At equal access time, choose the smallest stack that can fill the
missing magazine rounds; if none can, choose the largest stack. One reload
uses one stack and may partially fill the magazine. The duration snapshots
`reload_time_seconds` from the weapon's gameplay file plus the selected stack's
access time. Transfer occurs at completion; shooting, switching and discarding
wait for it. Retrieval leaves the stack in place even at zero.

Grenades have one loaded grenade. A successful throw automatically starts the
same retrieval process for another grenade of the matching `ammo_type`.
Both its shot cooldown and retrieval must complete before the next throw.
Multiplayer owns the quantities and reload timer on the server and publishes
them to the client HUD.

## Crafting supplies and physical parts

Equipment descriptors in `storage/items/` may set `stackable = true`. Counted
supplies such as nails use integer `max_stack` and instance `quantity` values,
as ammunition does. Tape and wire set `quantity_unit = "m"`; their authored
`max_stack` and `quantity` values are metres and may contain three decimal
places. Runtime quantities use integer millimetres, so taking 0.125 m consumes
125 quanta without accumulating floating-point rounding errors. Nonfinite,
negative, overflowing and submillimetre amounts are rejected. Omitting
`quantity_unit` preserves the existing counted-item behavior.

Narrow storage cells mark metre-based stacks with `m`; item details and the
Inventory and Crafting drawers show the complete length, including decimals.

Each stack starts under `[equipment.<instance-id>]`, referencing its descriptor
with `item` and providing `quantity`. Nonstackable equipment omits `quantity`.
For example, `item = "duct_tape"` and `quantity = 10.0` start a ten-metre roll.
Empty rolls remain owned and placed, following the existing ammo-stack policy.
Merging requires matching item types and quantity units; non-ammo supplies must
also share their catalog definition. Tape and wire never merge with each other
or with medical/count supplies merely because their ammo types are empty.

`PlayerInventory::quantity`, `setQuantity`, `consume`, ground transfers and
multiplayer snapshots all use integer quanta. Use `itemQuantityFromDisplay`
to convert authored quantities and `formatItemQuantity` for labels such as
`12.5 m`. Counted quantities retain their existing numeric values and display.
The fingerprinted descriptor determines the unit; network quantities retain
their integer representation.

Optional `[model]` / `[[model.parts]]` entries describe physical geometry and
named part materials in the same item descriptor. Part coordinates are local
Y-up metres. A broom's `handle` and `head` parts carry `material = "wood"`;
there is no separate component list to keep synchronized with its model.
These fields describe the item; crafting recipes and disassembly rules remain
separate responsibilities.

Runtime collision uses one shape per authored model part. Boxes retain their
offsets and dimensions; frusta use boxes spanning their widest footprint.
Each shape retains its part material. `wood` and `cardboard` material names,
or those item tags when a part has no material, permit nail placement.

The `nail` tag marks the ten-centimetre nail. Retrieving it in Crafting consumes
one counted unit rather than removing its entire stack. A suspended preview
uses an enclosing sphere for selection; fastening overlaps use the authored
nail parts. Driving moves the nail 0.099 m along its point, then connects every
intersected body with a revolute joint around the shaft. Grabbing that nail
again removes its fastening joints.

The `bind` tag enables tape/wire handling during a Crafting drag. Five upright
30 cm square cross-sections, separated by 0.01 m along their horizontal normal,
query the actual collider surfaces. Their clipped intersections supply both
the visible lines and body-local attachment points. Temporary tension springs
tighten intersected bodies with contacts enabled; release creates distance
limits for each shared plane. Completed ties belong to the world and survive
returning the binding item to inventory. Active line pairs are replicated by
multiplayer snapshots and disappear when a drag finishes or is cancelled.

## Current prototype boundary

There is one owned firearm per weapon ID, with multiple ammo stack instances.
Ground transfers update both inventory ownership and combat weapon ownership.
Weapons retain magazines/cooldowns; ammo retains stack quantities. Acquired ammo
with a colliding instance ID receives a new ID while retaining its catalog
prototype.
Multiplayer replays sequenced inventory
pickup, rotation, placement and cancellation through the server's `InventoryDrag`.
Snapshots update quantities during moves. Complete ownership and ground
corrections wait until queued operations are acknowledged and the local and
authoritative placement transactions are idle. The protocol version changes
with this dynamic ownership format.
New actions wait behind an earlier server placement timer. Placements and stack
changes last for the session and do not rewrite TOML.

The configuration formats and tag restrictions remain documented in
[the storage schema](../storage/SCHEMA.md) and
[weapon definitions](../storage/weapons/README.md).

## Weapon switching and theoretical access ranges

`WeaponSwitch` snapshots both storage accesses and both gameplay draw times
when a selection is confirmed. Draw time is also the old weapon's put-away
time. Storage timing uses `inventoryTransferSeconds()`: one base for a shared
compartment, otherwise both bases, plus the old weapon's backpack destination
adjustment and the new weapon's backpack source adjustment. Both obstruction
penalties and support bonuses apply. The pair is excluded from occupancy scoring.
Equipped weapons retain their reserved storage footprints. Timed switching
uses the current inventory on standalone and authoritative servers.

`inventoryAccessRange(grid, itemHeight)` calculates attainable limits across
all possible placements and other-item occupancy. With H grid rows and h item
rows, best access is `max(0, base - (H-h)*support_per_cell)`; worst is
`base + (H-h)*obstruction_per_cell`. Width does not affect the per-column scores.
External pockets without occupancy adjustments have equal best and worst times.
The inventory panel shows the overall main-grid range, using h=1:

| Backpack | Best | Worst |
| --- | ---: | ---: |
| Sling bag | 0.90 s | 1.54 s |
| Tactical backpack | 2.00 s | 4.16 s |
| Hiking backpack | 2.50 s | 6.40 s |

These are times for one access, before weapon draw/put-away costs. A taller
footprint narrows the range. The extrema place the item at the top with all
cells below occupied, or at the bottom with all cells above occupied.

Medical consumables live in `storage/medical/` and use `type = "medical"`,
stack limits and inventory footprints without weapon gameplay. The starting
inventory carries four bandages at `medical.bandages`; a bandage treats one
selected bleeding wound from the Health page and consumes one unit. Stale
requests, empty stacks and active storage transactions consume nothing.

## General equipment

`[equipment.<instance-id>]` in `inventory.toml` references an `item` definition
under `storage/items/`. These use inventory schema 2 with `type = "equipment"`,
cell dimensions, tags and an acoustics sidecar, and have no weapon gameplay
component or ammunition count. Each instance needs a placement. They are handled
through the Object handling equipment drawer and ordinary ground-inventory
transfers. The starter canopy uses 2×2 cells while folded; its deployed size and
articulated physics are independent of its stored footprint. The two starter
support stakes each use 1×6 cells. See [canopy handling](VIEWER.md#tent-canopy-and-support-stakes).
