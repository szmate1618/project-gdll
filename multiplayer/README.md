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
asset download occurs during startup. Maps and zombie assets must be GLBs.

Scene options:

- `--spawn X Z`: local east/south coordinates in meters; default is map center.
- `--zombies PATH --zombie-count N --zombie-radius METERS`: default is the existing
  zombie pack, 100 zombies within 100 m. Small scenes under 100 m in either
  horizontal dimension have no zombies. Maximum population is 1,024.
- `--no-trees`: omit detected trees and runtime forests on both peers.
- `--no-forests`: omit runtime forest generation on both peers.
- `--seconds N`: end a session after N seconds, useful for checks.
- `--lag-ms N --loss-percent N`: simulate outgoing packet delay and loss through
  GameNetworkingSockets. These testing options may differ between peers.

Client controls: WASD, mouse look, Shift run, Space jump, Ctrl crouch, Z prone,
1–4 weapon selection, R reload, left click fire, right click cycle scope.
Tab switches Combat/Interaction. In Interaction, hold either mouse button to
grab a nearby prop; left mouse also grabs doors. Mouse motion and walking move
the grip. Alt releases/recaptures the mouse; Escape exits. Losing focus releases
controls. Ammo and mode appear in the window title. Remote players are simple
colored bodies whose height follows their stance.

The existing single-player executable remains available:

```sh
./build/godollo_viewer output/godollo.glb --fps
```

## Simulation and ownership

The server owns player movement, weapon/ammo decisions, zombie perception and
target selection, door/prop interactions and ragdoll physics. Each zombie is
advanced once per tick across all players. It retains a visible target, or
selects the nearest visible player. Disconnects release grabs and invalidate
target identities. Two players cannot own the same grabbed object.

The server runs at 60 Hz, including two existing 120 Hz player substeps. It
sends self-contained snapshots at 20 Hz. One peer cannot advance the world by
sending more commands. Sequence numbers, bounded queues, redundant input
transmission, timeouts and rejection of malformed messages handle late,
duplicated and reordered traffic. Missing input is held briefly, then neutralized;
already queued commands still execute even after packets stop arriving.
After an input backlog builds up, the server processes two queued commands per
tick using one 120 Hz player substep each. This drains the queue without adding
movement time or speeding up the world. Button transitions stay ordered;
unreceived commands remain unacknowledged and are resent alongside the newest input.
Large replaceable messages use bounded datagram fragments, keeping them out of
GameNetworkingSockets' automatic large-message reliable queue. Incomplete older
snapshots do not block newer complete ones.

The local client predicts movement, restores the complete authoritative
movement state, and replays unresolved inputs. Other players, zombies, doors
and boxes use buffered snapshot interpolation. The server does not render, so
authoritative hit/ragdoll poses come from its animation clock.

Living zombies use the existing spatial index for interest queries. Entry/exit
radii are 220/240 m in all directions; offscreen threats still simulate.
Snapshots prioritize the nearest 256 zombies and stay under 256 KiB, including
corpse palettes. Doors are replicated near the observer; the bounded set of
interior boxes is sent in full, including to late joiners. Geometry keeps the
runtime X-east/Y-up/Z-south meter frame and player positions denote feet.
Unchanged corpse palettes are omitted only after a client acknowledges receiving
that version. Clients retain them even when an intermediate frame is not drawn.

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

Unit checks cover protocol validation, reconciliation across jumps/stances,
multi-player target selection, single-player equivalence, blocked shots, pose
replication, late-join corpses, command deduplication, fragment loss/reordering and
stale snapshots. A scripted transport check covers Hello arriving before the
Connected callback and verifies that the later callback preserves the joined peer.
They also cover recovery from repeated half-second and 1.5-second input
outages, retained button actions, queue draining after a packet timeout,
command gaps and the movement speed limit.
The process integration
test connects two real clients under artificial packet loss and latency,
exercises late join, and checks content mismatch rejection.

This is a first playable networking foundation, with eight-player admission and
512 interior boxes per session. Capacity/performance at those limits is not a
benchmark claim. Historical shot lag compensation, audio playback, player-player
collision, dynamic-prop prediction, general state delta compression, persistence and public
server authentication are not implemented. Shots use the current server pose
and respect map/door/prop occlusion. Dynamic objects remain server-owned, so
moving through or pushing them can cause client movement corrections. Existing
prototype zombie attack states do not yet apply player damage.
Large corpse-heavy snapshots still cost bandwidth and are more vulnerable to
packet loss; their packing and update frequency need profiling before scaling up.

Implementation lives in `protocol.*`, `network.*` and `scene.*` at this directory
root, `server/` for simulation/hosting, and `client/` for input, prediction,
snapshot playback and rendering. Shared `src/` additions expose movement
save/restore and remote physics poses; existing single-player APIs and defaults
are retained.
