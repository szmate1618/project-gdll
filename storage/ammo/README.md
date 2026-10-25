# Ammunition items and stacking schema

These files describe separate reserve-ammunition items for the current weapons.
They extend the [inventory item schema](../weapons/README.md#inventory-schema-v2)
with `type = "ammo"` and optional stacking fields. This is a data definition
only: the current C++ loader still reads weapon items only and does not load
this directory, validate the new fields, or implement stacks. Existing weapon
definitions, ownership, placements, reserve counts and combat behavior remain
unchanged. Packaging and multiplayer fingerprint integration are deferred with
loader support.

## Ammo item schema (v2 extension)

```toml
schema_version = 2
id = "pistol_rounds"
name = "Pistol rounds"
type = "ammo"
ammo_type = "pistol_rounds"
width = 1
height = 1
stackable = true
max_stack = 60
```

The filename is `<id>.inventory.toml`, with a matching `<id>.tags.txt` sidecar
in this directory. `id` is a unique item identifier; `name` is its display name.
`ammo_type` matches a weapon gameplay file's `ammo_type` and the corresponding
reserve key in root `inventory.toml`'s `[ammo]` table. Ammo items omit `weapon`
and have no gameplay file; firing behavior stays in the weapon definition.
Each unit represents one round, shell or grenade, rather than a magazine or box.

`width` and `height` retain the existing integer range of 1–16 inventory cells.
They describe the entire stack's footprint, independent of its current quantity.
All initial ammo footprints are 1×1; sizes and limits are editable prototype
values.

## Stacking fields for all item types

| Field | Type | Default when omitted | Meaning |
| --- | --- | --- | --- |
| `stackable` | Boolean | `false` | Whether multiple units of the same item may share one footprint |
| `max_stack` | Integer | `1` | Maximum number of units in one occupied footprint |

A non-stackable item must have `max_stack = 1`. A stackable item must specify
`max_stack` of at least 2. Only units with the same item ID may stack; matching
tags or matching `ammo_type` alone do not make different items interchangeable.
An occupied stack holds between 1 and `max_stack` units; zero units removes it.
The limit is per stack, not a total inventory capacity or magazine capacity.
Current quantity belongs to inventory state, not the item definition.

These fields are optional additions to schema version 2. Existing weapon files
can omit them and describe non-stackable items with a limit of 1. The defaults
and constraints above specify the future loader contract; C++ does not enforce
them yet. Root `[weapons]` values still mean loaded rounds, not stack quantities.

## Ammo types and tags

| Item ID / ammo type | Current weapon | Maximum stack | Tags |
| --- | --- | --- | --- |
| `pistol_rounds` | Pistol | 60 | `ammo, pistol_ammo` |
| `smg_rounds` | Submachine gun | 60 | `ammo, smg_ammo` |
| `shotgun_shells` | Shotgun | 24 | `ammo, shotgun_ammo` |
| `sniper_rounds` | Sniper | 30 | `ammo, sniper_ammo` |
| `he_grenades` | HE grenade | 6 | `ammo, grenade_ammo, explosive` |

Tags follow the existing comma-separated sidecar format: unique, nonempty
values containing lowercase letters, digits and underscores. `ammo` is the
common category; the other tags distinguish ammo families. Ammo does not carry
`handgun` or `long_gun`, so weapon-only holsters and slings do not accept it.

`he_grenades` describes reserve grenades for the existing `he_grenade` weapon
definition. It is not another weapon or an additional starting supply. No ammo
placements or stack quantities are added to root `inventory.toml` in this step.
