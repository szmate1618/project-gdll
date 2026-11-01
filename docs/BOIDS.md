# Fixed-point bird simulation

Only boid movement uses fixed-point arithmetic. Player movement, physics,
zombies, scene loading, geometry queries and rendering keep their existing
numeric representations. No global compiler floating-point settings change.

## Modules and ownership

- `src/boid/fixed.hpp`: integer units, checked narrowing, division, square root,
  vector scaling and integration remainders.
- `src/boid/simulation.hpp/.cpp`: pure deterministic movement and state. No
  clock, scene queries, floating-point arithmetic or client-local AI decisions.
- `src/boid/flock_neighbors.hpp/.cpp`: bounded spatial sampling for local
  separation and alignment, rebuilt from each flock's pre-tick positions.
- `src/boid/bird_flock.hpp/.cpp`: authority decisions, bounded scene queries,
  player perception and swept damage. The standalone viewer also uses this.
- `src/boid/bird_flight_space.hpp/.cpp`: the existing float collision/canopy
  query boundary. Only the authority validates routes and selects targets.
- `src/boid/presentation.hpp`, `bird_renderer.hpp/.cpp`: float conversion and
  existing card rendering. Wing animation never feeds movement.
- `src/boid/replica.hpp/.cpp`: checkpoint restore, ordered command replay and
  presentation interpolation.
- `multiplayer/server/birds.hpp/.cpp`: command history and per-publication hash
  caching. `World` applies authoritative bird damage to existing player vitals.

## Numerical contract

Positions use signed 32-bit millimetres, velocity uses mm/s, and acceleration
uses mm/s². Coordinates are limited to +/-100 km on each axis. Queries outside
the supported frame fail explicitly. Products and flock sums use wider integer
intermediates. Signed division truncates toward zero, integer square root rounds
down, and checked narrowing rejects overflow instead of relying on signed wrap.

Every movement tick is exactly 1/60 second. Signed per-axis velocity and position
remainders preserve fractions between ticks, so slow movement is not discarded.
Movement rules and neighbor sampling are part of simulation version 2. Birds
have no assigned formation slots. They align with nearby birds within 1.5 m,
separate within 1 m, and receive cohesion only outside a loose 3 m radius
(1.2 m during gathering). Smooth individual acceleration changes every two
seconds, allowing birds to exchange positions while following a shared route.
Swoops steer toward the committed aim/exit with local separation and alignment.

A per-flock spatial hash examines at most eight cells and eight bucket entries
per cell, including hash collisions. Crowded-cell samples rotate by integer
tick, rather than permanently choosing the same birds. All steering reads the
same pre-tick positions and velocities before integration. Storage is reserved
at restore and reused; neighbor work stays bounded per bird. Spawn offsets,
steering variation and render phases derive from stable unsigned integer hashes.
Checkpoints include all inputs needed to rebuild this scratch index and resume
the exact motion. Version 1 checkpoints are rejected because their movement
rules differ; server and clients must use the same simulation version.

The standalone `update(dt, player)` adapter accumulates presentation-frame time
into those fixed steps. Network replicas never use frame deltas for movement:
they advance to server tick watermarks. Authority-only float queries quantize
their resulting targets once; clients receive those integers verbatim.

## Multiplayer timing

Multiplayer birds are opt-in with `--birds` on the server or hosted/local game.
There are six flocks of 192 birds by default. Existing standalone count controls
remain available; multiplayer checkpoint decoding is bounded to 4,096 birds.

The server simulates birds at 60 Hz and publishes a reliable ordered bird frame
at the existing 20 Hz send cadence. A frame contains a completed-through tick,
state checksum and all flock commands after that peer's previous accepted send.
Commands contain an effective tick, flock index and integer control state:
destination, attack aim/exit/direction, mode, remaining ticks and pass identifier.
No server command can alter an already completed tick.

A client at tick 100 receiving a frame completed through 103 applies each
command at its original tick while advancing 101, 102 and 103. It never predicts
beyond the completed watermark, so late delivery does not require rollback or
historical simulation snapshots. Other game systems continue independently.
Presentation interpolates recent float poses against the existing delayed
remote-render clock; severe delivery delays freeze bird playback until data
arrives. Reliable bird updates share the regular reliable lane; a large join
checkpoint can temporarily increase that lane's backlog.

The authority picks at most one bird for perception per tick and rotates
candidate players. Only it queries geometry and applies swept-contact damage.
Health uses the existing authoritative player snapshot field. Clients do not
report damage or choose bird targets.

## Checkpoints, recovery and memory

A checkpoint is the complete **replica movement state** at one tick: schema
version, seed, integer bird positions/velocities/integration remainders, pass
flags, flock ranges, means and controls. It does not migrate authority-only
route-selection RNG, player-damage bookkeeping or world geometry.

The server sends a current checkpoint on join, explicit checksum recovery, or
when a peer exceeds the ten-second retained-command window or the 1,024-command
batch budget. There is no periodic full-state timer. A healthy client may only
receive its initial checkpoint. Each checksum is computed once per published
server tick and reused for all peers. Checksums cover canonical integer fields,
never struct padding or floating-point render data.

Server history holds only commands for the last 600 ticks. The client's current
integer simulation is the base state for replay; historical rollback states are
not stored. At the default population, one live checkpoint has 60,456 bytes of
state payload and encodes into a 57,036-byte protocol message. Authority working
data, neighbor-index/acceleration scratch, vector/deque bookkeeping and float
presentation caches are extra.
Each retained command is 72 bytes in the measured Linux build. Six flocks can
produce at most 3,600 commands in 600 ticks: 259,200 bytes of payload in the
worst case, with container overhead additional.

Client visual history is separate: at most 40 published poses, about 1.29 MB of
payload for 1,152 birds, plus the displayed pose and current simulation. It
retains two seconds at 20 Hz; normal sampling discards older poses earlier.

## Verification and costs

```bash
cmake --build build --target test_boid_simulation test_bird_flock \
  test_multiplayer_birds test_multiplayer_transport benchmark_bird_flock \
  benchmark_bird_network -j4
ctest --test-dir build -R '^(viewer_boid_simulation|viewer_bird_flock|multiplayer_birds|multiplayer_transport)$' --output-on-failure
./build/benchmark_bird_flock
./build/multiplayer/benchmark_bird_network
```

The integer regression includes a fixed golden checksum after 3,720 ticks and
checkpoint continuation. It also varies the CPU floating-point rounding mode,
checks arithmetic boundaries and integration remainders, and tests duplicate
frames, delayed commands, mismatch detection and late-join restore. Windows CI
builds and runs this same golden-state check and the multiplayer bird suite.
Running it on Linux alone does not constitute Windows runtime validation.
Behavior regressions also check local separation across negative grid
boundaries, alignment, coincident-bird escape, isolation between flocks, and
continued internal movement and reordering after twenty seconds of travel.

Multiplayer tests round-trip signed protocol fields, reject malformed states,
compare clients with different delivery schedules, verify bounded-history
fallback, and exercise real reliable delivery under packet loss alongside
existing world snapshots and corpse-pose traffic. GPU checks retain silhouette,
depth and GL-state coverage.

On the Ryzen 5 7535HS, Release, synthetic open world, version 2 averaged
0.449/0.412 ms per idle/active update for 1,152 birds and 5.188/5.181 ms for
12,288 (2026-11-01, 120 warmup ticks followed by 3,600 exact 60 Hz ticks per
case). Historical version 1 formation steering averaged 0.058/0.075 ms for
1,152 birds and 0.532/0.793 ms for 12,288. Version 2 trades higher CPU cost for
local neighbor reactions and a changing flock shape. Construction and rendering
are excluded. Actual town geometry query costs can differ.

The default-state microbenchmark measured approximately 0.001 ms to copy a
checkpoint, 0.055 ms to hash movement state, 0.121 ms to encode a checkpoint,
0.063 ms to decode and 0.004 ms to restore the integer core. Full replica join
handling (restore, checksum, presentation) took about 0.060 ms. Checkpoint work
is occasional; hashing at 20 Hz costs about 1.1 ms of CPU time per second.

The one-minute standalone authority test emitted 89 commands and retained a
peak of 19 commands (1,368 bytes payload). The two-player multiplayer test sent
200 frames and 14 commands in ten simulated seconds: 6,966 payload bytes,
excluding its initial 57,036-byte checkpoint and transport overhead. These are
fixture measurements, not bandwidth guarantees for every gameplay situation.
