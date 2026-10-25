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
| Pick up, rotate, place, swap and cancel a placement transaction | `src/inventory/inventory_drag.hpp/.cpp` |
| Draw storage grids and item details | `src/gui/inventory_view.hpp/.cpp` |
| Convert pointer/wheel input and hit tests into inventory operations | `src/gui/inventory_interaction.hpp/.cpp` |
| Present configured action shortcuts | `src/gui/action_hotbar.hpp/.cpp` |
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

`InventoryDrag` borrows the model and is the placement transaction service; it
contains no pointer coordinates or GUI code. The GUI uses it, and gameplay can
use the same operations. Only the controller that began a transaction can finish
or cancel it. Cancelling or destroying that controller restores the entire
unfinished chain; completed free-space drops persist. Storage and item maps
cannot be modified directly through the public model API.

Pickup is immediate. `drop()` validates the destination and starts `pending()`
for source plus destination compartment access time. `advance(seconds)` takes
elapsed simulation/session seconds and commits the placement when time is up.
Pending items still report `Hand` until completion. A displaced item is picked
up at completion and charges destination access only on its next placement.
Further drops/rotation are blocked during a pending placement. `cancel()` needs
no time and restores the whole unfinished chain. The GUI consumes the caller's
`HudState::seconds` clock, which continues while menus are open, and draws a
progress bar beside the pending destination. There is no game pause.

## Current prototype boundary

There is one owned instance per item/weapon ID. Acquisition/removal, multiple
instances of the same definition, stacking, ammo accounting,
equipping and network replication are separate future gameplay work. The combat
ownership/ammo state is not synchronized with this storage model yet. The refactor
provides shared definitions and a reusable query/placement model without changing
those mechanics. Placements still last for the session and do not rewrite TOML.

The configuration formats and tag restrictions remain documented in
[the storage schema](../storage/SCHEMA.md) and
[weapon definitions](../storage/weapons/README.md).
