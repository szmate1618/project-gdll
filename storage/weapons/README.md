# Weapon configuration

Each weapon keeps its inventory description, gameplay settings and tags together
under `storage/weapons/`:

```text
pistol.inventory.toml    Inventory name and N×M footprint (schema version 2)
pistol.gameplay.toml     Firing, ammo, muzzle and other gameplay settings
pistol.tags.txt          Comma-separated category tags
```

| Path | Responsibility |
| --- | --- |
| `storage/weapons/<id>.inventory.toml` | Item label, N×M cell footprint and gameplay reference |
| `storage/weapons/<id>.gameplay.toml` | Weapon behavior and combat HUD label |
| `storage/weapons/<id>.tags.txt` | Shared tags for that weapon |
| `inventory.toml` | Player ownership, loaded rounds, reserves, placements and action shortcuts |
| `storage/equipment.toml`, `storage/backpacks.toml` | Available storage equipment and compartments |
| `storage/loadout.toml` | Equipped backpack and body storage |

## Inventory schema (v2)

```toml
schema_version = 2
id = "pistol"
name = "Pistol"
type = "weapon"
weapon = "pistol"
width = 2
height = 1
```

`width` and `height` are **N columns × M rows of inventory cells**, not meters,
model dimensions or pixel sizes. Each must be an integer from 1 to 16. These
starter footprints are editable prototype values:

| Item | Footprint |
| --- | --- |
| Pistol | 2×1 |
| Submachine gun | 3×2 |
| Shotgun | 4×2 |
| Sniper | 5×2 |
| HE grenade | 1×1 |

The filename prefix, `id`, and `weapon` must match. `weapon` references the
matching `<id>.gameplay.toml`. This prototype supports `type = "weapon"` only.
`name` is the inventory display name. No icon, texture, font or model asset is
specified here.

## Tags files

Tags are read from `<id>.tags.txt`, independently of the inventory TOML:

```text
handgun, ranged
```

Use one or more comma-separated tags, without quotes. Surrounding spaces and
newlines are trimmed. Tags contain lowercase letters, digits and underscores;
empty values, duplicate tags and missing files are rejected. Inventory schema
version 2 replaces the old inline `tags` array with this sidecar file. Storage
catalogs retain their independent version 1 schema.

| Weapon | Tags |
| --- | --- |
| Pistol | `handgun, ranged` |
| HE grenade | `grenade, throwable, explosive` |
| Submachine gun | `long_gun, smg, ranged, two_handed` |
| Shotgun | `long_gun, shotgun, ranged, two_handed` |
| Sniper | `long_gun, rifle, ranged, two_handed` |

`long_gun` is the common category for our rifles, SMG and shotgun. The specific
weapon tags are retained alongside it. Storage compartments may restrict accepted
items using `allowed_tags`; see [the storage schema](../SCHEMA.md#item-tag-restrictions).

## Storage placements

Root `inventory.toml` gives each owned item one placement:

```toml
[placements.pistol]
storage = "body/hip_holster/holster"
x = 0
y = 0
rotation = 0 # Optional clockwise degrees: 0, 90, 180 or 270.
```

`storage` references an **equipped** compartment. Valid reference forms are:

- `body/<equipment-id>/<slot-id>`
- `backpack/<backpack-id>/main` for the internal grid
- `backpack/<backpack-id>/<slot-id>` for an external pocket

The IDs come from the storage catalogs and `storage/loadout.toml`. The slot ID
`main` is reserved for the backpack's internal grid. Item `x` and `y` are
**zero-based integer cell coordinates of its top left**, with X increasing to
the right and Y downward. These are different from the normalized panel
coordinates used to position storage compartments.

The rectangle size comes from the item descriptor's `width` and `height`; a
placement cannot override it. At 90° or 270°, width and height exchange axes.
On opening Inventory, all owned items must have
one placement, references must resolve to equipped storage, and rectangles must
fit without overlapping. Touching edges are allowed. Missing/stale placements,
unequipped compartments, disallowed item tags, invalid coordinates, oversized items and overlaps
produce configuration errors rather than clipping or silently moving items.

The starter pistol is 2×1 in the hip holster, and the grenade is in the chest
pouch. Left and right shoulder slings are also equipped, each with a vertical
2×5 grid. They fit the 5×2 sniper after a 90° wheel rotation while held; the
current weapon placements stay in the backpack, holster and pouch. The SMG, shotgun and sniper are in the tactical backpack. Those three
weapons need 24 cells, so the starter loadout now equips the existing 8×10
backpack instead of the 20-cell sling bag. The 4×5 sling-bag definition remains
available in the catalog.

The UI draws each occupied footprint as a labeled/tinted rectangle on the
compartment grid; tiny body-storage items also have a name below the grid.
Drag items with the left mouse button and rotate them with the wheel in 90°
steps. Valid drops wait for source plus destination access, with a progress bar
beside the destination; an item displaced at completion pays destination access
only on its next placement. A completed free-space placement finishes the move.
Overlapping multiple items or extending outside the grid is rejected.
Right-click, leaving Inventory or losing focus restores the whole unfinished swap
chain. Completed moves persist in memory until the session ends; no configuration
files are written. Automatic packing, stacking and runtime
ownership synchronization are not implemented. Existing grenade/ammunition counts and combat behavior
are unchanged.

## Ownership and action hotbar

The existing `[weapons]` table in root `inventory.toml` is the ownership source:
a key means that weapon is owned, and its value is its starting loaded round
count. Zero rounds still means an owned weapon. This is not an item footprint
or a count of firearm instances; the current grenade/ammo rules are unchanged.
There is no second list of owned weapons to keep synchronized.

`[hotbar]` maps slots `0`–`9` to owned item IDs. Omit a slot to leave it unassigned:

```toml
[hotbar]
0 = "pistol"
1 = "submachine_gun"
2 = "shotgun"
3 = "sniper"
4 = "he_grenade"
```

All five weapons start owned and assigned in that order; slots 5–9 are empty.
Shortcuts are references, so they add no ownership or storage capacity. Unknown
or unowned IDs and invalid slot numbers are rejected. Inventory and Skills use
the same player-owned model, loaded once at startup. Restart to apply
file edits. These remain visual menu shortcuts; weapon input bindings and
runtime weapon ownership changes are not connected to this menu snapshot yet.

Gameplay reads `storage/weapons/*.gameplay.toml` and ownership/ammo from `inventory.toml` at startup.
Inventory descriptors, gameplay definitions, tag files and the player inventory
are included in multiplayer fingerprints and content packs, with LF line endings
for cross-platform byte consistency.

Weapon gameplay TOMLs require `draw_time_seconds`, a finite nonnegative number
of seconds. It is used for both drawing that weapon and putting it away.
A confirmed change adds the old put-away time, both storage access times and
the new draw time. Obstruction is charged once for the higher-scoring footprint;
base and support adjustments apply to both. See [weapon switching](../../docs/INVENTORY.md#weapon-switching-and-theoretical-access-ranges).
