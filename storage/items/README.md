# Shelter equipment

General equipment definitions use inventory schema 2 and `type = "equipment"`.
They have `.inventory.toml`, `.tags.txt` and `.acoustics.toml` sidecars, without a
weapon gameplay file. `[equipment.<instance>]` in root `inventory.toml` references
the definition with `item = "<id>"`; placements use the existing storage API.

- `canopy`: 2×2 packed cells, unfolding into a 3×3 m sheet of 36 rigid panels.
- `support_stake`: 1×6 cells, a second 1.5 m wooden support with the same point-first
  flight and embedding as the equippable stake.

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
