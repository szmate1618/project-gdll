# Inventory code and ownership

The inventory model has no OpenGL, RmlUi or window dependencies. Its CMake
targets are defined in `cmake/Inventory.cmake` for graphical and headless builds.

| Responsibility | Module |
| --- | --- |
| Item definition with inventory/gameplay components and shared tags | `src/inventory/item.hpp/.cpp` |
| Read one item's three definition files | `src/inventory/item_config.hpp/.cpp` |
| Parse weapon gameplay settings for both items and combat | `src/weapon_config.hpp/.cpp` |
| Read player ownership, placements and shortcuts | `src/inventory/inventory_config.hpp/.cpp` |
| Read storage catalogs and equipped loadout | `src/inventory/storage_config.hpp/.cpp` |
| Own player items, equipped storage, placements and held item; answer queries | `src/inventory/player_inventory.hpp/.cpp` |
| Resolve compartments and validate footprints, tags and overlaps | `src/inventory/inventory_placement.hpp/.cpp` |
| Score backpack obstruction/support and calculate adjusted access times | `src/inventory/inventory_access.hpp/.cpp` |
| Pick up, rotate, place, swap and cancel a placement transaction | `src/inventory/inventory_drag.hpp/.cpp` |
| Draw storage grids and item details | `src/gui/inventory_view.hpp/.cpp` |
| Convert pointer/wheel input and hit tests into inventory operations | `src/gui/inventory_interaction.hpp/.cpp` |
| Present configured action shortcuts | `src/gui/action_hotbar.hpp/.cpp` |
| Preview and confirm ten action shortcuts | `src/action_selection.hpp/.cpp` |
| Calculate and advance timed weapon changes | `src/weapon_switch.hpp/.cpp` |
| Coordinate GUI documents and rendering | `src/gui/hud.hpp/.cpp` |

## Item components

`Item` is an immutable definition. `inventory()` returns its display name, type
and N×M footprint, parsed from `<id>.inventory.toml`. `gameplay()` returns the
actual `WeaponConfig`, parsed from `<id>.gameplay.toml`, including firing/ammo
settings and grenade parameters. `tags()` and `hasTag()` use `<id>.tags.txt`.
Combat's `Weapons` uses the same gameplay parser; there is no second inventory
interpretation of the gameplay schema.

Mutable magazine rounds, reserves and cooldowns remain in `Weapons`. Mutable
storage locations belong to `PlayerInventory`. Changing one player's placement
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

## Current prototype boundary

There is one owned instance per item/weapon ID. Acquisition/removal, multiple
instances of the same definition, stacking, ammo accounting,
and placement network replication remain future work. The combat
ownership/ammo state is not synchronized with this storage model yet. The refactor
provides shared definitions and a reusable query/placement model without changing
those mechanics. Placements still last for the session and do not rewrite TOML.

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
uses the current standalone inventory; server players use their startup layout.

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
