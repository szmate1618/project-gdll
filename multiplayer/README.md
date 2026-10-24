# Multiplayer prototype

The original `godollo_viewer` keeps its entry point, local simulation loop and
controls. Multiplayer is optional and has separate client/server executables.
GameNetworkingSockets handles UDP transport; the code here owns commands,
authoritative simulation, snapshots, prediction and interpolation.

## Build

Use the existing viewer dependencies, plus protobuf headers and its compiler.
On Debian/Ubuntu:

```sh
sudo apt install libssl-dev libprotobuf-dev protobuf-compiler
cmake -S . -B build -DGD_MULTIPLAYER=ON
cmake --build build -j4
```

CMake uses an installed GameNetworkingSockets package when available, otherwise
downloads pinned, SHA-256-checked v1.4.1 sources into `build/`. A normal build
with `GD_MULTIPLAYER=OFF` does not find/download/link networking dependencies.

For a server machine without OpenGL, GLFW, display or audio dependencies:

```sh
cmake -S . -B build-server -DGD_MULTIPLAYER=ON -DGD_VIEWER=OFF
cmake --build build-server -j4
```

This also builds a headless client for network checks. Existing viewer graphics
tests are excluded in this configuration; multiplayer tests still run.

Only scenario loading requires the `.navmesh` beside its `.glb` map. For the
included horde scenario:

```sh
./build/multiplayer/godollo_server output/godollo.glb --scenario scenarios/test-horde/scenario.toml
./build/multiplayer/godollo_client output/godollo.glb --zombie-count 1600
```

The server calculates all scenario routes during startup and advances them even
without connected players. Ordinary zombies never request navmesh paths or
require a cache. Player perception can hand a scenario actor to original direct
behavior movement permanently; it releases the route and never searches again.
Clients receive positions through existing snapshots
and do not need a navmesh or scenario file. See the [scenario configuration](../docs/ZOMBIE_NAVIGATION_RUNTIME.md#test-horde-scenario)
for world X/Z meter rectangles and destination behavior.

## Linux client package

The **Build apps** workflow also uploads `godollo-client-linux-x86_64`, a tarball
for Ubuntu 24.04 x86-64 and compatible newer desktop Linux systems. Extract it
and use `./run-client.sh`; the launcher selects its bundled libraries and resolves
relative scene paths from the package folder. Resources are located beside the
executable. The package includes the small sample scene, shaders, configuration,
and library notices. The full town uses the separate asset pack below.
See [Linux package instructions](linux/README.md) for requirements and commands.
CI checks the extracted package's real networking, resource lookup, and rendering
under Xvfb/Mesa software rendering. This does not validate hardware performance.

## Windows client

The **Build apps** GitHub Actions workflow also builds the graphical client on
`windows-2022`. It uses MSVC, GLEW for OpenGL loading, and the pinned vcpkg
manifest in `windows/`. Protobuf stays at 3.21.12 for compatibility with the
existing GameNetworkingSockets build. Linux retains its current OpenGL setup.

Download `godollo-client-windows-x86_64` from the workflow run and extract the
enclosed ZIP. It includes the client, shaders, item/inventory definitions,
forest configuration, licenses and the synthetic test scene. Windows looks for
these resources beside the executable, with the source checkout as a development
fallback. It needs an OpenGL 3.3 driver and the Visual C++ x64 Redistributable.
See [package usage](windows/README.md) for commands and matching town assets.
Git attributes preserve LF endings in fingerprinted configuration files so
Windows checkout conversion does not create a content mismatch with Linux.
Scene settings use fixed-width little-endian values in the fingerprint, independent
of platform float formatting or locale. Both peers must use the updated format;
older builds are rejected as a content mismatch.

The Windows job builds the multiplayer client and checks `--help` startup and
the scene fingerprint against cross-platform reference hashes.
It does not run graphics or gameplay tests. The original viewer, headless server,
and regression suites continue to be built/tested on Linux.

## Portable asset pack

The default preset is `osm-tuned2` in the viewer, client, server and asset
packager: OSM forest polygons plus a combined OSM tree/row and tuned2 layer.
Prepare it with `tools/prepare_trees.py` before packaging. The package includes
`output/trees.instances.json`, `output/osm/forest_patches.json`, and their shared
runtime assets. Separate raw tree inputs and orthophoto backups are excluded.
`--vegetation orthophoto|osm|sentinel-astra|none` selects another preset in both
the packager and client/server. Use the same preset on both peers;
`asset-pack.json` records the packaged choice.

Package the already prepared town on the host machine, using Python's standard
library only. On Linux:

```sh
.venv/bin/python tools/package_multiplayer_assets.py
```

The same script works on Windows with `py tools/package_multiplayer_assets.py`.
It writes `build/godollo-assets.zip`, shared by Windows and Linux. It includes
the map and adjacent tree/pole/guardrail layers, referenced tree models and forest
textures, zombie models and their referenced LoDs, weapon/inventory definitions,
forest configuration, and the text notices in `credits/assets/`. It excludes executables,
shaders, source models, generator datasets, caches, previews, and backups.

Use `--map PATH`, `--zombies PATH`, or `--output PATH.zip` for other prepared
inputs/output. Input assets must stay within this checkout; runtime references must be
relative and use `/`, and model assets must be self-contained GLBs. The script
preserves all file bytes so existing fingerprints remain valid. Missing files,
stale provenance, and files changed during export prevent publication; a failed
export preserves the previous ZIP. No downloads or map generation occur.

Both client packages include the same `credits/assets/` folder and point to it
from their root README. The asset ZIP adds `credits/assets/MAP.txt`, built from
the published GLB's selected scene metadata and bound to its SHA-256. Sentinel
credits use the recorded acquisition year; adjacent reports or attribution
files may belong to another generation and are not used as the source of this
notice. Missing credit files or a missing Sentinel acquisition date prevent
publication. Existing attribution embedded in the map is retained in the notice.

The asset pack also generates `credits/assets/VEGETATION.txt` from its selected
published tree/forest documents. It records their hashes, counts and Sentinel
reference dates independently of the map texture. Generated-artwork provenance
and a runtime/preparation software inventory accompany both archives under
`credits/`; JSON/stb notices are also present in the client `licenses/` folder.
The preparation programs themselves are not distributed in the town asset ZIP.

The notices cover OpenStreetMap, Sentinel-2, Copernicus DEM GLO-30, CC0 Durango
weapons, and the conditional zombie/audio assets. They do not clear other
licensing obligations. Before public distribution, present the OSM credit
visibly in the game or its credits, and address any ODbL adapted-database
share-alike/access obligations. The recorded zombie Standard license restricts
standalone/extractable model redistribution: confirm the acquired license and
permitted delivery method before sharing model-containing asset ZIPs, or use
`--no-zombies` for sessions with `--zombie-count 0`. The orthophoto-derived tree
and forest placement sources still need a separate license review before public
distribution; that review is deferred while those assets are being replaced.

For sessions with disabled layers, use `--no-trees` (also omits forests),
`--no-forests`, and/or `--no-zombies`. The latter corresponds to the game's
`--zombie-count 0`. `asset-pack.json` inside the archive records the selected
paths/options and SHA-256 of every included file. Spawn, zombie count/radius,
and vegetation options still need to match between server and clients.

Extract into a fresh destination for each pack version, keeping the archive's
folder layout. For either packaged client, first extract the client archive
there, then the asset ZIP beside `godollo_client` or `godollo_client.exe`.
Use `./run-client.sh` for the packaged Linux client. Linux source builds fall
back to their checkout for resources, so extract there before building/running.
The original single-player executable still uses its build checkout's paths.
Windows compilation and runtime checks remain in GitHub CI.

## Run

From the repository root, start the server, then clients in separate terminals:

```sh
./build/multiplayer/godollo_server output/godollo.glb
./build/multiplayer/godollo_client output/godollo.glb --connect 127.0.0.1:27020
```

Use the server's numeric IP for other machines. The server listens on UDP port
27020; change it with `--port`. `--connect` accepts numeric IPv4/IPv6 and a port.
These direct-IP sessions have encrypted transport but no account authentication
or matchmaking. Run this prototype with trusted peers; no relay service is set up.

Both peers must use matching scene options and files. The handshake verifies
protocol version, map, adjacent pole/guardrail layers, tree manifests/assets,
forest configuration, zombie GLBs, weapon definitions, inventory and spawn.
Existing manifest provenance checks remain enabled. A stale adjacent layer is
an error, so both peers load the same collision world. No town regeneration or
asset download occurs during startup. Model assets must be self-contained GLBs;
external buffer/image URIs are rejected even inside a `.glb`. Embedded BIN data
and data URIs are supported. Zombie assets may have at most 128 skeleton nodes
(including non-joint scene nodes); incompatible rigs fail before hosting.
Weapon definitions and starting inventory are loaded once per scene and copied
for each joining player, so changing files cannot change a running session.
The current wire protocol is version 12; rebuild both client and server together.
Snapshots include persistent head/torso wound masks and authoritative loose
cavity contents. Clients render these without running their own cavity physics.

Scene options:

- `--spawn X Z`: local east/south coordinates in meters; default is map center.
- `--zombies PATH --zombie-count N --zombie-radius METERS`: default is the existing
  zombie pack, 100 zombies within 100 m. Small scenes under 100 m in either
  horizontal dimension have no zombies. Maximum population is 1,024.
- `--no-trees`: omit detected trees and runtime forests on both peers.
- `--no-forests`: omit runtime forest generation on both peers.
- Server-only `--no-zombie-perception`: skip zombie activation scans, behavior
  creation, sight/hearing and attacks. Authored scenario routes, shooting and
  ragdolls remain active. Clients need no matching flag.
- `--seconds N`: end a session after N seconds, useful for checks.
- `--time-of-day H --day-length S`: shared day/night clock, starting at noon
  with a 1,200-second day by default. Zero day length freezes time. Use
  `--time-of-day 0 --day-length 0` on both peers for fixed night and headlights.
- `--lag-ms N --loss-percent N`: simulate outgoing packet delay and loss through
  GameNetworkingSockets. These testing options may differ between peers.

Client controls: WASD, mouse look, Shift run, Space jump, Ctrl crouch, Z prone,
1–5 weapon selection, the physical key left of 1 (Hungarian 0 / US backtick)
for fists, R reload, F toggle flashlight, left click fire/throw/punch, right click
cycle scope.
Tab switches Combat/Interaction. In Interaction, hold either mouse button to
grab a nearby prop; left mouse also grabs doors. Mouse motion and walking move
the grip. F12 releases/recaptures the mouse; Escape exits. Losing focus releases
controls. Ammo and mode appear in the window title. Remote players are simple
colored bodies whose height follows their stance. With fists equipped, their
physical torso and arms replace that simple body. Local hands show the newest
server-confirmed pose, translated to the predicted eye; they do not wait for the
remote interpolation buffer. Punches therefore include network latency.

At level load both peers validate the optional `abandoned_cars.instances.json`
beside the map and include its exact bytes in the content fingerprint. The server
spawns every parked car and randomly chooses its type once per session. Clients
receive those types and poses from the server, including on late join; they never
rerandomize the population. Up to 1,024 cars fit the runtime/wire population limit.
The existing 256 KiB snapshot budget still bounds all entity populations together.
Full car poses are currently included in every snapshot, so large populations
increase network traffic; vehicle interest filtering is not implemented yet.
If the sidecar is absent, the server places one random shared car near the initial
spawn when clear supported terrain is available within 25 m. An empty sidecar
suppresses this fallback. Walk beside a car's configured entry point and press
Enter to enter the indicated free seat. Up to
four clients share one car in distinct seats; the front-left seat drives. The
window title shows the car name and available seat inside its configured radius.
WASD drives, Space brakes, Enter
exits below 1.5 m/s when there is a clear standing position, and Alt toggles
first/third person while driving. F12 continues to release/recapture the mouse.
Passengers can only mouse-look and leave with Enter; they cannot drive, select
weapons, scope, grab or choose the chase camera. Remote occupants are hidden
inside the prototype chassis. The server owns seat reservations and releases
them on exit or disconnect. Clients share optional [vehicle LoD assets](../assets/vehicles/README.md#vehicle-lods) by type,
with a procedural fallback when the local cache is unavailable. See [vehicle definitions](../vehicles/README.md)
for names, masses, seat entry/exit points, wheels and headlights. Definitions are
loaded once per scene, fingerprinted, and shipped with client and asset packages.
Parked server cars use two static boxes; only driver entry (or an explicit
gameplay impulse) enables wheel/suspension physics. After the driver leaves or
disconnects, the car brakes and returns to boxes once settled. Passengers do not
activate it. Client replicas retain poses and velocity using static colliders
without local suspension bodies.
The car has the same suspension, directional grip, steering response, reduced
reverse performance and zombie push/ragdoll impacts as [single-player](../docs/VIEWER.md#prototype-vehicles).

Combat normal view shows the same camera-local 1911, UMP, scoped M14 sniper and
pump shotgun as single-player, including the eye-height hand/shoulder poses.
The displayed model follows the server-confirmed equipped slot; a pending or
rejected selection keeps the current model. Scope and Interaction views hide
the weapon. Only scoped weapons enable zoom, and a confirmed weapon change
resets zoom. Remote players retain their simple body models.

Slot 5 throws a grey/red octagonal HE grenade on left click. The server owns
its 3-second fuse, shared prop physics, 8 m occluded blast, and outward impulses.
Clients interpolate thrown bodies and show the short explosion point light;
the flashlight toggle does not disable that light. Grenade settings and starting
counts are in `items/he_grenade.toml` and `inventory.toml`.

Graphical client builds generate the five small weapon GLBs offline from the
tracked [CC0 mesh source](../assets/weapons/README.md). Linux and Windows client
packages include these models and attribution under `assets/weapons/`; no
separate weapon download is needed. Development clients use the build tree's
generated assets. Headless clients and servers do not load weapon graphics.

The existing single-player executable remains available:

```sh
./build/godollo_viewer output/godollo.glb --fps
```

## Simulation and ownership

The server owns player movement, weapon/ammo decisions, zombie perception and
target selection, door/prop interactions and ragdoll physics. Each zombie is
advanced once per tick across all players. It retains a visible target, or
selects the nearest visible player. Disconnects release grabs and invalidate
target identities. Two players cannot own the same grabbed object or driver seat.
Cars use the same shared rigid-body world, with one physics update per server
tick regardless of input traffic. Input timeout applies brakes while preserving
the driver's seat; disconnect releases the seat and applies brakes. Parked cars
remain available, including to late joiners.
Nearby living zombies use dynamic upright capsules in that world, including
their touching crowd neighbors beyond the activation radius. The server alone
advances their force-driven movement and publishes resulting feet positions.
Car, crowd, wall, door and object contact responses share the same solver;
high-speed contact transfers capsule motion into the existing ragdoll system.

The server runs at 60 Hz, including two existing 120 Hz player substeps. It
sends essential world snapshots at 20 Hz. One peer cannot advance the world by
sending more commands. Sequence numbers, bounded queues, redundant input
transmission, timeouts and rejection of malformed messages handle late,
duplicated and reordered traffic. Missing input is held briefly, then neutralized;
already queued commands still execute even after packets stop arriving.
After an input backlog builds up, the server processes two queued commands per
tick using one 120 Hz player substep each. This drains the queue without adding
movement time or speeding up the world. Button transitions stay ordered;
unreceived commands remain unacknowledged and are resent alongside the newest input.
Large replaceable messages use paced datagram fragments, keeping them out of
GameNetworkingSockets' automatic large-message reliable queue. Each connection
retains one active transfer and only its newest pending snapshot. A temporary
send failure resumes the active fragment; it never starts another incomplete
prefix. Queued essential data is bounded to roughly 10 ms of estimated link
capacity, capped at 16 KiB. Incomplete received snapshots do not block newer
complete ones.

The local client predicts movement, restores the complete authoritative
movement state, and replays unresolved inputs. Other players, zombies, doors
and boxes use buffered snapshot interpolation. Zombie root rotation uses
quaternions with the fixed asset normalization kept separate, preserving size
through turns. Remote players draw in the opaque pass before glass and rain.
The server does not render, so
authoritative hit/ragdoll poses come from its animation clock.
Cars replicate rigid chassis and individual wheel poses plus chassis velocity;
wheel steering, suspension travel and rotation remain visible to every client.
All cars are included in essential snapshots (up to 1,024), including random
parked placements loaded beside the map. The driver camera follows the same interpolated pose as
the rendered car. Vehicle motion is server-authoritative and is not locally
predicted, so driving response includes network latency. Driving commands are
retained/resubmitted normally but never replayed as walking movement. Lighting
uses the shared server tick and matching day/night settings, including parked
headlights at night.

Living zombies use the existing spatial index for interest queries. Entry/exit
radii are 220/240 m in all directions; offscreen threats still simulate.
Snapshots prioritize the nearest 256 zombies and stay under 256 KiB without
corpse palettes. Doors are replicated near the observer; the bounded set of
interior boxes is sent in full, including to late joiners. Geometry keeps the
runtime X-east/Y-up/Z-south meter frame and player positions denote feet.
Corpse poses carry independently versioned roots and palettes on a separate
reliable transport lane. Each peer has at most four unacknowledged poses and a
32 KiB bulk transport budget. Changes while a pose is in flight coalesce to the
latest version; round-robin selection prevents moving corpses starving others.
Protocol version 6 includes the prepared joint-separation index in each complete
corpse pose, so the mesh variant and bone palette are applied together. Joint
definitions and referenced capped meshes participate in the scene fingerprint
and asset packaging. Clients use the server's detached-body motion directly.
The essential/bulk lanes share bandwidth at 4:1 when both are busy, so bulk
retries cannot block essential delivery and bulk still makes progress. An
explicit pose acknowledgement releases a transfer slot; snapshot acknowledgements
cannot suppress an unreceived palette. Late joins receive their own initial
poses. Clients retain complete root/palette pairs outside the display buffer and
apply them together without blending affine bone matrices. Corpses stay hidden
until their first complete pose arrives.

Physical melee runs at the shared server physics rate. The server validates
fist ownership, individual clicks and the 0.5 s cooldown. It builds one set of
nearby zombie contact proxies for all players, reads every striking arm's solved
contacts, then applies the combined hit reactions. Guard and returning arms do
not damage zombies. Interaction mode, vehicle occupancy, weapon switching and
disconnect disable or release the player rig. Fists are always available and
cannot become dropped equippable items. Melee targets zombies; there is no
player-health or player-versus-player damage system.

Protocol 12 includes each enabled player's nine physical box poses and hand
state in essential snapshots. Positions, quaternion orientations and dimensions
keep the maximum fleet, interested zombies, doors, boxes and eight melee players
inside the existing 256 KiB message limit. Invalid/non-rigid or oversized poses
are rejected. Remote body rotations interpolate as quaternions without changing
box dimensions. Zombie hit reactions use the existing complete, independently
versioned corpse-pose lane, including repeat impacts and late joins. Clients
never run melee physics or decide hits.

## Checks and current limits

```sh
ctest --test-dir build -R '^multiplayer_' --output-on-failure
./build/multiplayer/godollo_client assets/test.glb --no-trees --zombie-count 0 \
  --headless --bot --seconds 5
```

The second command requires a matching server. `--headless` never creates a
window. `--bot` walks forward for repeatable movement checks. A graphical client
can save its final framebuffer with `--screenshot build/multiplayer.ppm`.
`--hidden` keeps that graphical window invisible; `--look YAW PITCH` selects an
initial view for repeatable captures. Both still require a display/GPU.

Unit checks include `multiplayer_melee` for two-player physical hits, duplicate
commands, alternating hands, late joins, body interpolation, disconnect cleanup
and the full snapshot budget. Graphics checks cover confirmed first-person
punches without firearm effects, the physical key binding, and remote physical
bodies behind glass. Other checks cover protocol validation, reconciliation across jumps/stances,
multi-player target selection, single-player equivalence, blocked shots, pose
replication, late-join corpses, command deduplication, fragment loss/reordering and
stale snapshots. Focused checks cover startup rejection of external GLB resources
and oversized rigs, the 128-node boundary, rigid turning through a 500 ms gap,
bounded/fair corpse transfers, reordered pose/state arrival, and interrupted
fragment sends. A real transport regression exercises large snapshots without
packet loss and continuous bulk transfers with 5% loss in both directions. A GPU
regression checks remote players behind and in front of transparent glass.
A scripted transport check covers Hello arriving before the
Connected callback and verifies that the later callback preserves the joined peer.
They also cover recovery from repeated half-second and 1.5-second input
outages, retained button actions, queue draining after a packet timeout,
command gaps and the movement speed limit.
The process integration
test connects two real clients under artificial packet loss and latency,
exercises late join, and checks content mismatch rejection.
Vehicle regressions cover exclusive seats, duplicate Enter commands, blocked
moving exits, timeout braking, disconnect/re-entry, late-join wheel poses,
interpolation, zombie push/ragdoll replication, and equivalence to single-player
physics. The graphical client regression also exercises Enter, Alt, F12 and
first/third person driving views.

This is a first playable networking foundation, with eight-player admission and
512 interior boxes per session. Capacity/performance at those limits is not a
benchmark claim. Local predicted shrub rustles play through OpenAL (`--no-audio`
disables playback); shrub noise for zombie hearing is calculated on the server.
Remote shrub sounds and other multiplayer audio are not implemented yet.
Historical shot lag compensation, player-player
collision, dynamic-prop prediction, general state delta compression, persistence and public
server authentication are not implemented. Shots use the current server pose
and respect map/door/prop occlusion. Dynamic objects remain server-owned, so
moving through or pushing them can cause client movement corrections. Existing
prototype zombie attack states do not yet apply player damage.
Corpse poses still cost bandwidth and may update less frequently on slow links;
their packing and update frequency need profiling before scaling up. Essential
snapshots still use unreliable fragments, so loss can drop a complete world
update; the next complete snapshot supersedes it.

Implementation lives in `protocol.*`, `network.*`, `outgoing_snapshots.*`,
`asset_validation.*` and `scene.*` at this directory root. `server/replication.*`
owns per-peer interest and corpse transfers; `server/world.*` owns simulation.
`server/vehicle_seats.*` owns exclusive seat admission and driver-only controls;
shared `src/vehicle_config.*` and `src/vehicle_player.*` define seats and entry/exit geometry.
The remaining `server/` code hosts sessions, and `client/` handles input, prediction,
snapshot playback and rendering. Shared `src/` additions expose movement
save/restore and remote physics poses; existing single-player APIs and defaults
are retained.
