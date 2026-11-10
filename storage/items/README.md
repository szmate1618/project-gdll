# General items and shelter equipment

General equipment definitions use inventory schema 2 and `type = "equipment"`.
They have an `.inventory.toml` descriptor and `.acoustics.toml` impact profile,
without a weapon gameplay file. Tags can be declared in the descriptor as
`tags = ["crafting", "bind"]`; existing definitions retain their `.tags.txt`
sidecars. `[equipment.<instance>]` in root `inventory.toml` references the
definition with `item = "<id>"`; placements use the existing storage API.

## Crafting supplies

The Crafting drawer lists carried items with the `crafting` or `bind` tag.
The starter backpack contains:

| Item | Inventory cells | Starting quantity | Tags |
| --- | --- | --- | --- |
| Duct tape | 1×1 | 10 m | `crafting`, `bind` |
| Spool of wire | 1×1 | 20 m | `crafting`, `bind` |
| Nails | 1×1 | 20 | `crafting`, `nail` |
| Broom | 1×6 | 1 | `crafting` |

Tape and wire define `stackable = true`, `quantity_unit = "m"` and
`max_stack = 50.0`. Their root inventory `quantity` is a length in metres and
may be fractional; it is not a count of rolls or spools. Counted stacks such as
nails omit `quantity_unit` and use an integer `max_stack` and `quantity`.
The broom is a single, nonstackable item. The inventory cell footprint is
separate from the model's physical dimensions.

## Primitive item models

The same inventory TOML describes each item's low-poly model through a `[model]`
table with a positive `mass_kg`, followed by separate `[[model.parts]]` records.
Every part has a unique `name`, `shape`, `material`, `center`, `size` and RGB
`color`. Coordinates and full dimensions are in metres in the runtime frame:
X right/east, Y up, Z forward/south. Colors are normalized values from 0 to 1.

`shape = "box"` uses all three full dimensions in `size`. A rectangular
`shape = "frustum"` uses `size.x` and `size.z` for its lower footprint,
`size.y` for its full height, and `top_size = [width, depth]` for the upper
footprint. `center` is the midpoint of that height. Each closed primitive has
12 triangles, keeping the models deliberately simple.

Tape is a small grey box; wire is a blue-grey box. The steel nail is exactly
10 cm long, with a tapered rectangular shaft and a box head. The 1.30 m broom
has three separately described parts: a straw-colored `fiber` brush frustum,
a `wood` box head and a long `wood` box handle. Per-part materials remain item
metadata and identify nailable wood/cardboard surfaces; the separate acoustic
profile describes the overall object's impacts. Collision uses each primitive
separately, with rectangular boxes around frusta. The space beside the broom's
narrow handle stays empty.

Crafting retrieves one nail at a time. Its tip snaps onto a wood/cardboard hit,
and rotation pivots at that point. A parked nail uses an enclosing sphere for
selection. Clicking it drives it 9.9 cm inward; the actual nail parts determine
which intersected bodies receive revolute hinges around its shaft.

## Shelter equipment

- `canopy`: 2×2 packed cells, unfolding into a 3×3 m sheet of 36 rigid panels.
- `support_stake`: 1×6 cells, a 1.5 m wooden support with the same point-first
  flight and embedding as the equippable stake. Two start in the backpack.

Use the Object handling equipment drawer to take these items into the hand.
G stores them in the backpack. The ordinary stake remains equippable on slot 8.

`src/canopy.hpp/.cpp` owns the 35 additional bodies and 60 shared-side spring
hinges; `GroundItems` owns the representative panel and inventory lifetime.
Every panel resolves to its owning ground item for grabbing and retrieval.
The hinges return toward a flat sheet at 1.5 Hz with a 0.9 damping ratio. Each
plate weighs 80 g. Shared negative collision groups suppress sheet self-contact;
world, prop and stake contacts remain active. Embedded stakes retain canopy
contacts while staying fixed to their existing anchor. Ground-item snapshots
include all 36 poses; replicas create the same panels without solving hinges.
