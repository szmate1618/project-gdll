# Inventory item definitions (v1)

These files describe equippable items as inventory objects. Firing, ammunition,
recoil, muzzle coordinates and grenade behavior belong in `weapons/*.toml`.
The five existing gameplay files were moved there without changing their values.

| Path | Responsibility |
| --- | --- |
| `items/<id>.toml` | Item label, N×M cell footprint, tags and gameplay reference |
| `weapons/<id>.toml` | Weapon behavior and existing combat HUD label |
| `inventory.toml` | Player weapon ownership, loaded rounds, reserves and action shortcuts |
| `storage/equipment.toml`, `storage/backpacks.toml` | Available storage equipment and compartments |
| `storage/loadout.toml` | Equipped backpack and body storage |

## Item schema

```toml
schema_version = 1
id = "pistol"
name = "Pistol"
type = "weapon"
weapon = "pistol"
width = 2
height = 1
tags = ["handgun", "ranged"]
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

The filename stem, `id`, and `weapon` must match; `weapon` references
`weapons/<id>.toml`. Version 1 supports `type = "weapon"` only. `name` is the
inventory display name. `tags` is an array of unique nonempty category strings;
`[]` is valid. No icon, texture, font or model asset is specified here.

The item dimensions and tags are parsed and retained for the inventory UI.
Placement, fitting, rotation, stacking and tag restrictions are not implemented
in this prototype. A large footprint does not automatically occupy storage or
resize the backpack. Storage grids remain empty until placements are defined.

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
the same cached data, read once when either screen first opens. Restart to apply
file edits. These remain visual menu shortcuts; weapon input bindings and
runtime weapon ownership changes are not connected to this menu snapshot yet.

Gameplay reads `weapons/` and ownership/ammo from `inventory.toml` at startup.
Both catalogs and inventory are included in multiplayer fingerprints and content
packs, with LF line endings for cross-platform byte consistency.
